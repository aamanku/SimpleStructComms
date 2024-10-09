/*
MIT License

Copyright (c) 2024 Abhijeet Kulkarni

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
#ifndef SIMPLESTRUCTCOMMSTWOWAY_HPP
#define SIMPLESTRUCTCOMMSTWOWAY_HPP

#include <arpa/inet.h>
#include <bits/stdc++.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// For server
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#define UDP_HANDSHAKE_MAGIC_WORD "Giggle"
#define UDP_BUFFER_SIZE 1024
#define UDP_LOOP_SLEEP_US 100
#define UDP_CLIENT_RECONNECT_TIMEOUT_MS 10 // 100ms (does not work for 1s)
#define INFO_PRINT_ON 1
#define DEBUG_PRINT_ON 0

#define IS_VALID_DATA_TYPE(T) \
  static_assert(std::is_trivially_copyable<T>::value, #T " contains non-trivial types."); \
  static_assert(std::is_trivial<T>::value, #T " contains non-trivial constructors or destructors."); \
  static_assert(std::is_standard_layout<T>::value, #T " does not have a standard layout."); \
  static_assert(sizeof(T) < UDP_BUFFER_SIZE, #T "Data size is too large for buffer");

template<typename T_send, typename T_recv>
class UDPComms
{
	IS_VALID_DATA_TYPE(T_send)
	IS_VALID_DATA_TYPE(T_recv)

protected:
	const char* listen_ip_;
	uint16_t listen_port_;
	uint16_t send_port_;
	int timeout_ms_;
	bool initiator_;

	int sockfd_listen_, sockfd_send_;
	struct sockaddr_in servaddr_listen_, servaddr_send_, cliaddr_;

	std::pair<T_send, bool> send_data_; // Data to be sent and flag to indicate new data
	std::pair<T_recv, bool> recv_data_; // Data to be received and flag to indicate new data
	std::mutex mtx_send_, mtx_recv_;
	std::condition_variable cv_send_, cv_recv_;
	std::thread send_thread_, recv_thread_;

	const char* kMagicWord = UDP_HANDSHAKE_MAGIC_WORD;
	const size_t kSizeSendData = sizeof(T_send);
	const size_t kSizeRecvData = sizeof(T_recv);
	constexpr static int buffer_size_ = UDP_BUFFER_SIZE;
	char buffer_send_[buffer_size_];
	char buffer_recv_[buffer_size_];

	bool server_running_ = false;
	bool keep_running_ = true;

	enum class ServerState
	{
		kInitializing = 0, kWaitingForClient, kConnected
	};
	ServerState server_state_ = ServerState::kInitializing;

	enum class ClientState
	{
		kInitializing = 0, kWatingForServer, kConnected
	};
	ClientState client_state_ = ClientState::kInitializing;
	std::chrono::time_point<std::chrono::system_clock> client_last_seen_;

public:
	UDPComms(const char* listen_ip, uint16_t listen_port, uint16_t send_port, bool initiator, int timeout_ms)
		: listen_ip_(listen_ip), listen_port_(listen_port), send_port_(send_port), initiator_(initiator), timeout_ms_(timeout_ms)
	{
		// Creating socket file descriptor for listening
		if ((sockfd_listen_ = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		{
			perror("listen socket creation failed");
			exit(EXIT_FAILURE);
		}

		// Creating socket file descriptor for sending
		if ((sockfd_send_ = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		{
			perror("send socket creation failed");
			exit(EXIT_FAILURE);
		}

		memset(&servaddr_listen_, 0, sizeof(servaddr_listen_));
		memset(&servaddr_send_, 0, sizeof(servaddr_send_));

		// Filling server information for sending (this is the server)
		servaddr_send_.sin_family = AF_INET;
		servaddr_send_.sin_port = htons(send_port_);
		servaddr_send_.sin_addr.s_addr = INADDR_ANY;

		// Filling server information for listening
		servaddr_listen_.sin_family = AF_INET;
		servaddr_listen_.sin_port = htons(listen_port_);
		servaddr_listen_.sin_addr.s_addr = inet_addr(listen_ip_);

		// Bind the socket with the server address
		if (bind(sockfd_send_, (const struct sockaddr*)&servaddr_send_,
			sizeof(servaddr_send_)) < 0)
		{
			perror("bind failed");
			exit(EXIT_FAILURE);
		}

		// Timeout for receiving data
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 1000 * UDP_CLIENT_RECONNECT_TIMEOUT_MS; // 100 ms

		setsockopt(sockfd_listen_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		// start threads
		send_thread_ = std::thread(&UDPComms::sender_main_loop_, this);
		recv_thread_ = std::thread(&UDPComms::receiver_main_loop_, this);
	}
	~UDPComms()
	{
		log_info_("Shutting down UDPComms");
		keep_running_ = false;
		send_thread_.join();
		recv_thread_.join();
		close(sockfd_listen_);
		close(sockfd_send_);
		log_info_("UDPComms shutdown complete");
	}

	/// @brief Stream data to the other end
	/// @param data Data to be streamed
	void stream_data(T_send& data)
	{
		std::lock_guard<std::mutex> lock(mtx_send_);
		// Copy observation to internal value
		send_data_.first = data;
		send_data_.second = true;

		// Notify server thread
		cv_send_.notify_one();
	}

	/// @brief Get data from the other end. This function will make the data stale
	/// after reading.
	/// @param data Data to be received
	/// @return true If data was received
	bool get_data(T_recv& data)
	{
		std::lock_guard<std::mutex> lock(mtx_recv_);
		// copy data to the output
		data = recv_data_.first;
		bool was_new_data = recv_data_.second;
		recv_data_.second = false;
		return was_new_data;
	}

	/// @brief Check if new data is available
	/// @return true If new data is available
	bool is_data_new()
	{
		std::lock_guard<std::mutex> lock(mtx_recv_);
		return recv_data_.second;
	}

//	bool is_connected()
//	{
//	}

protected:
	void sender_main_loop_()
	{
		server_state_ = ServerState::kWaitingForClient;

		while (keep_running_)
		{
			switch (server_state_)
			{
			case ServerState::kInitializing:
				log_error_("Should not be in initializing state");
				exit(EXIT_FAILURE);
				break;
			case ServerState::kWaitingForClient:
				try_connect_to_client_();
				std::this_thread::sleep_for(
					std::chrono::microseconds(UDP_LOOP_SLEEP_US));
				break;
			case ServerState::kConnected:
				// Wait till notified
			{
				std::unique_lock<std::mutex> lock(mtx_send_);
				cv_send_.wait_until(lock, std::chrono::system_clock::now() +
					std::chrono::milliseconds(timeout_ms_));
			}
				check_heartbeat_();
				send_data_from_memory_();
				break;
			default:
				log_error_("Invalid state");
				exit(EXIT_FAILURE);
				break;
			}
		}
	}

	void try_connect_to_client_()
	{
		if (server_state_ == ServerState::kWaitingForClient)
		{
			log_info_("Waiting for client to connect on port: " +
				std::to_string(send_port_));
			if (get_magic_word_from_client_())
			{
				server_state_ = ServerState::kConnected;
				log_info_("Connected to client on port: " + std::to_string(send_port_) +
					" with IP: " + inet_ntoa(cliaddr_.sin_addr));
				client_last_seen_ = std::chrono::system_clock::now();
			}
			else
			{
				log_error_("Received word: " + std::string(buffer_send_) +
					" from IP: " + inet_ntoa(cliaddr_.sin_addr) +
					" which is not the magic word");
				log_error_("Expected magic word: " + std::string(kMagicWord));
			}
		}
		else
		{
			log_error_(
				"Already connected to client on port: " + std::to_string(send_port_) +
					" with IP: " + inet_ntoa(cliaddr_.sin_addr));
		}
	}

	/**
 * @brief Get the magic word from the client.
 *
 * @param blocking If true, the function will block until data is received.
 * @return true If the magic word is correct.
 * @return false If the magic word is incorrect.
 */
	bool get_magic_word_from_client_(bool blocking = true)
	{
		int n;
		auto len = sizeof(cliaddr_); // len is value/result

		auto is_blocking = blocking ? MSG_WAITALL : MSG_DONTWAIT;

		n = recvfrom(sockfd_send_, (char*)buffer_send_, buffer_size_,
			is_blocking, (struct sockaddr*)&cliaddr_,
			(socklen_t*)&len);
		if (n > 0)
		{
			buffer_send_[n] = '\0';
			bool is_magic_word_correct = strcmp(buffer_send_, kMagicWord) == 0;

			if (!is_magic_word_correct)
			{
				log_error_("Received word: " + std::string(buffer_send_) + " from IP: " + inet_ntoa(cliaddr_.sin_addr) + " which is not the magic word");
				log_error_("Expected magic word: " + std::string(kMagicWord));
			}

			return is_magic_word_correct;
		}
		return false;
	}

	/**
	 * @brief Check the heartbeat of the client.
	 */
	void check_heartbeat_()
	{
		if (timeout_ms_ < 0)
		{
			return;
		}

		auto now = std::chrono::system_clock::now();
		auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - client_last_seen_).count();
		if (diff > timeout_ms_)
		{
			server_state_ = ServerState::kWaitingForClient;
			log_error_("Client on port: " + std::to_string(send_port_) + " with IP: " + inet_ntoa(cliaddr_.sin_addr) + " timed out");
		}

		if (get_magic_word_from_client_(false))
		{
			client_last_seen_ = std::chrono::system_clock::now();
		}
	}

	/**
     * @brief Send data to the client.
     */
	void send_data_from_memory_()
	{
		std::lock_guard<std::mutex> lock(mtx_send_);
		if (send_data_.second == false)
		{
			return;
		}
		else
		{
			sendto(sockfd_send_, (const char *)&send_data_.first, kSizeSendData,
				MSG_CONFIRM, (const struct sockaddr *)&cliaddr_,
				sizeof(cliaddr_));

			send_data_.second = false;
		}
	}

	void receiver_main_loop_()
	{
		client_state_ = ClientState::kWatingForServer;
		while (keep_running_)
		{
			// Wait till timeout for data
			bool data_received = false;
			while (!data_received and keep_running_)
			{
				send_magic_word_();
				data_received = receive_data_();

				if (!data_received)
				{
					std::this_thread::sleep_for(std::chrono::microseconds(UDP_LOOP_SLEEP_US));
				}
			}
		}
	}

	/**
     * @brief Receive data from the server.
     *
     * @return true If data is received successfully.
     * @return false If data is not received.
     */
	bool receive_data_()
	{
		int n;

		socklen_t len = sizeof(servaddr_listen_);
		n = recvfrom(sockfd_listen_, (char *)buffer_recv_, buffer_size_,
			MSG_WAITALL, (struct sockaddr *)&servaddr_listen_,
			&len);
		log_debug_("Received buffer size: " + std::to_string(n));
		if (n <= 0) // No data received
		{
			client_state_ = ClientState::kWatingForServer;
			return false;
		}
		buffer_recv_[n] = '\0';

		// Check if received data is of correct size
		if (n != kSizeRecvData)
		{
			log_error_("Received data size: " + std::to_string(n) + " is not equal to expected size: " + std::to_string(kSizeRecvData));
			exit(EXIT_FAILURE);
			return false;
		}

		if (client_state_ == ClientState::kWatingForServer)
		{
			log_info_("Connected to server on port: " + std::to_string(listen_port_) + " with IP: " + listen_ip_);
			client_state_ = ClientState::kConnected;
		}

		// Copy data to output
		std::lock_guard<std::mutex> lock(mtx_recv_);
		memcpy(&recv_data_.first, buffer_recv_, kSizeRecvData);
		recv_data_.second = true; // New data is available
		return true;
	}

	/**
     * @brief Send magic word to server to establish connection.
     */
	void send_magic_word_()
	{
		// Convert sizeof(T_data) to bytes
		log_debug_("Sending magic word: " + std::string(kMagicWord) + " to server on port: " + std::to_string(listen_port_) + " with IP: " + listen_ip_);

		sendto(sockfd_listen_, kMagicWord, strlen(kMagicWord),
			MSG_DONTWAIT, (const struct sockaddr *)&servaddr_listen_,
			sizeof(servaddr_listen_));
	}







	///////////////////////////


	/**
	 * @brief Log informational messages.
	 *
	 * @param info Informational message.
	 */
	virtual void log_info_(const std::string& info)
	{
#if not INFO_PRINT_ON
		return;
#endif
		std::cout<<"[INFO]["<<std::this_thread::get_id()<<"] "<<info<<std::endl;
	}

	/**
	 * @brief Log error messages.
	 *
	 * @param error Error message.
	 */
	virtual void log_error_(const std::string& error)
	{
		std::cout<<"[ERROR]["<<std::this_thread::get_id()<<"] "<<error<<std::endl;
		std::cerr.flush();
	}

	/**
	 * @brief Log debug messages.
	 *
	 * @param debug Debug message.
	 */
	virtual void log_debug_(const std::string& debug)
	{
#if not DEBUG_PRINT_ON
		return;
#endif
		std::cout<<"[DEBUG]["<<std::this_thread::get_id()<<"] "<<debug<<std::endl;
		std::cout << debug << std::endl;
	}
};

#undef UDP_HANDSHAKE_MAGIC_WORD
#undef UDP_BUFFER_SIZE
#undef UDP_LOOP_SLEEP_US
#undef UDP_CLIENT_RECONNECT_TIMEOUT_MS
#undef INFO_PRINT_ON
#undef DEBUG_PRINT_ON
#endif // SIMPLESTRUCTCOMMSTWOWAY_HPP
