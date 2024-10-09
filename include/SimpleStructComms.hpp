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
#ifndef SIMPLESTRUCTCOMMS_HPP
#define SIMPLESTRUCTCOMMS_HPP

#include <bits/stdc++.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// For server
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>

#define UDP_HANDSHAKE_MAGIC_WORD "Giggle"
#define UDP_BUFFER_SIZE 1024
#define UDP_LOOP_SLEEP_US 100
#define UDP_CLIENT_RECONNECT_TIMEOUT_MS 10 // 100ms (does not work for 1s)
#define INFO_PRINT_ON 1
#define DEBUG_PRINT_ON 0

#define IS_VALID_DATA_TYPE(T)\
    static_assert(std::is_trivially_copyable<T>::value, #T " contains non-trivial types.");\
    static_assert(std::is_trivial<T>::value, #T " contains non-trivial constructors or destructors.");\
    static_assert(std::is_standard_layout<T>::value, #T " does not have a standard layout.");\
    static_assert(sizeof(T) < UDP_BUFFER_SIZE, #T "Data size is too large for buffer");\

/**
 * @brief UDPServer class template for handling UDP server operations.
 * 
 * @tparam T_data Type of data to be sent/received. Must be trivially copyable, trivial, and have a standard layout.
 */
template <typename T_data>
class UDPServer
{
    IS_VALID_DATA_TYPE(T_data)

public:
    /**
     * @brief Construct a new UDPServer object
     * 
     * @param port Port number to bind the server.
     * @param timeout_ms Timeout in milliseconds for client connection.
     */
    UDPServer(uint16_t port, int timeout_ms = -1) : port_(port), timeout_ms_(timeout_ms)
    {
        std::cout << "Magic word: " << std::string(kMagicWord) << std::endl;
        // Creating socket file descriptor
        if ((sockfd_ = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            log_error_("socket creation failed");
            exit(EXIT_FAILURE);
        }

        memset(&servaddr_, 0, sizeof(servaddr_));
        memset(&cliaddr_, 0, sizeof(cliaddr_));

        // Filling server information
        servaddr_.sin_family = AF_INET; // IPv4
        servaddr_.sin_addr.s_addr = INADDR_ANY;
        servaddr_.sin_port = htons(port_);

        // Bind the socket with the server address
        if (bind(sockfd_, (const struct sockaddr *)&servaddr_, sizeof(servaddr_)) < 0)
        {
            log_error_("bind failed");
            exit(EXIT_FAILURE);
        }

    }

    void start_server_thread()
    {
        if(state_ == UDPServer::kInitializing && !server_running_)
        {
            server_thread_ = std::thread(&UDPServer::server_main_loop_, this);
            server_running_ = true;
            state_ = UDPServer::kWaitingForClient;
        }
        else
        {
            log_error_("Server is already running");
        }
    }

    /**
     * @brief Set the data to be sent to the client.
     * 
     * @param value Data to be sent.
     */
    void set_data(const T_data &value)
    {
        if(server_running_)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            // Copy observation to internal value
            data_.value = value;
            data_.new_data = true;

            // Notify server thread
            cv_.notify_one();
        }
        else
        {
            log_error_("Server is not running");
            exit(EXIT_FAILURE);
        }
    }

    /**
     * @brief Directly send data to the client. This function is blocking.
     * 
     * @param value Data to be sent.
     */
    void send_data(const T_data &value)
    {
        if(server_running_)
        {
            log_error_("Server should not be running to use this function");
            exit(EXIT_FAILURE);
        }
        else
        {
            if(get_magic_word_from_client_(true))
            {
                sendto(sockfd_, (const char *)&value, sizeof(value),
                   MSG_DONTWAIT, (const struct sockaddr *)&cliaddr_,
                   sizeof(cliaddr_));
            }
            
        }
    }
    

    /**
     * @brief Stop the server.
     */
    void stop_server()
    {
        keep_running_ = false;
    }

    /**
     * @brief Check if the server is running.
     * 
     * @return true If the server is running.
     * @return false If the server is not running.
     */
    bool is_running()
    {
        return keep_running_;
    }

    /**
     * @brief Check if the server is connected to a client.
     * 
     * @return true If the server is connected to a client.
     * @return false If the server is not connected to a client.
     */
    bool is_connected()
    {
        return state_ == UDPServer::kConnected;
    }

    /**
     * @brief Destroy the UDPServer object
     */
    ~UDPServer()
    {
        close(sockfd_);
    }

protected:
    uint16_t port_;
    int timeout_ms_;
    const char *kMagicWord = UDP_HANDSHAKE_MAGIC_WORD;
    const size_t kSizeData = sizeof(T_data);
    constexpr static int buffer_size_ = UDP_BUFFER_SIZE;
    char buffer_[buffer_size_];
    std::thread server_thread_;

    int sockfd_;
    struct sockaddr_in servaddr_, cliaddr_;
    std::chrono::time_point<std::chrono::system_clock> client_last_seen_;
    bool keep_running_ = true;
    bool server_running_ = false;

    struct
    {
        T_data value;
        bool new_data = true;
    } data_;

    std::condition_variable cv_;// Filling server information for sending

    std::mutex mtx_;

    enum ServerState
    {
        kInitializing = 0,
        kWaitingForClient,
        kConnected
    };

    ServerState state_ = kInitializing;

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

        n = recvfrom(sockfd_, (char *)buffer_, buffer_size_,
                     is_blocking, (struct sockaddr *)&cliaddr_,
                     (socklen_t *)&len);
        if (n > 0)
        {
            buffer_[n] = '\0';
            bool is_magic_word_correct = strcmp(buffer_, kMagicWord) == 0;

            if (!is_magic_word_correct)
            {
                log_error_("Received word: " + std::string(buffer_) + " from IP: " + inet_ntoa(cliaddr_.sin_addr) + " which is not the magic word");
                log_error_("Expected magic word: " + std::string(kMagicWord));
            }

            return is_magic_word_correct;
        }
        return false;
    }

    /**
     * @brief Try to connect to a client.
     */
    void try_connect_()
    {
        if (state_ == UDPServer::kWaitingForClient)
        {
            log_info_("Waiting for client to connect on port: " + std::to_string(port_));
            if (get_magic_word_from_client_())
            {
                state_ = UDPServer::kConnected;
                log_info_("Connected to client on port: " + std::to_string(port_) + " with IP: " + inet_ntoa(cliaddr_.sin_addr));
                client_last_seen_ = std::chrono::system_clock::now();
            }
            else
            {
                log_error_("Received word: " + std::string(buffer_) + " from IP: " + inet_ntoa(cliaddr_.sin_addr) + " which is not the magic word");
                log_error_("Expected magic word: " + std::string(kMagicWord));
            }
        }
        else
        {
            log_error_("Already connected to client on port: " + std::to_string(port_) + " with IP: " + inet_ntoa(cliaddr_.sin_addr));
        }
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
            state_ = UDPServer::kWaitingForClient;
            log_error_("Client on port: " + std::to_string(port_) + " with IP: " + inet_ntoa(cliaddr_.sin_addr) + " timed out");
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
        std::lock_guard<std::mutex> lock(mtx_);
        if (data_.new_data == false)
        {
            return;
        }
        else
        {
            sendto(sockfd_, (const char *)&data_.value, sizeof(data_.value),
                   MSG_CONFIRM, (const struct sockaddr *)&cliaddr_,
                   sizeof(cliaddr_));

            data_.new_data = false;
        }
    }

    /**
     * @brief Main loop of the server.
     */
    void server_main_loop_()
    {
        while (keep_running_)
        {
            switch (state_)
            {
            case UDPServer::kInitializing:
                log_error_("Should not be in initializing state");
                exit(EXIT_FAILURE);
                break;
            case UDPServer::kWaitingForClient:
                try_connect_();
                std::this_thread::sleep_for(std::chrono::microseconds(UDP_LOOP_SLEEP_US));
                break;
            case UDPServer::kConnected:
                // Wait till notified
                {
                    std::unique_lock<std::mutex> lock(mtx_);
                    cv_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms_));
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

    /**
     * @brief Log informational messages.
     * 
     * @param info Informational message.
     */
    virtual void log_info_(const std::string &info)
    {
#if not INFO_PRINT_ON
        return;
#endif
        std::cout << info << std::endl;
    }

    /**
     * @brief Log error messages.
     * 
     * @param error Error message.
     */
    virtual void log_error_(const std::string &error)
    {
        std::cerr << error << std::endl;
        std::cerr.flush();
    }

    /**
     * @brief Log debug messages.
     * 
     * @param debug Debug message.
     */
    virtual void log_debug_(const std::string &debug)
    {
#if not DEBUG_PRINT_ON
        return;
#endif
        std::cout << debug << std::endl;
    }
};

/**
 * @brief UDPClient class template for handling UDP client operations.
 * 
 * @tparam T_data Type of data to be sent/received. Must be trivially copyable, trivial, and have a standard layout.
 */
template <typename T_data>
class UDPClient
{
    IS_VALID_DATA_TYPE(T_data)

public:
    /**
     * @brief Construct a new UDPClient object
     * 
     * @param server_ip IP address of the server.
     * @param port Port number to connect to the server.
     * @param timeout_ms Timeout in milliseconds for server connection.
     */
    UDPClient(const char *server_ip, uint16_t port, int timeout_ms) : server_ip_(server_ip), port_(port), timeout_ms_(timeout_ms)
    {
        // Creating socket file descriptor
        if ((sockfd_ = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        memset(&servaddr_, 0, sizeof(servaddr_));

        // Filling server information
        servaddr_.sin_family = AF_INET;
        servaddr_.sin_port = htons(port_);
        // Server address
        servaddr_.sin_addr.s_addr = inet_addr(server_ip_);

        state_ = ClientState::kWatingForServer;

        // Timeout for receiving data with select
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000 * UDP_CLIENT_RECONNECT_TIMEOUT_MS; // 100 ms

        setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    /**
     * @brief Get data from the server.
     * 
     * @param data Reference to the data object where received data will be stored.
     */
    void get_data(T_data &data)
    {
        // Wait till timeout for data
        bool data_received = false;
        while (!data_received)
        {
            send_magic_word_();
            data_received = receive_data_(data);

            if(!data_received)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(UDP_LOOP_SLEEP_US));
            }
        }
    }

protected:
    const char *server_ip_;
    uint16_t port_;
    int sockfd_;
    struct sockaddr_in servaddr_;
    int timeout_ms_;
    std::chrono::time_point<std::chrono::system_clock> server_last_seen_;

    const char *kMagicWord = UDP_HANDSHAKE_MAGIC_WORD;
    const size_t kSizeData = sizeof(T_data);
    char buffer_[UDP_BUFFER_SIZE];
    constexpr static int buffer_size_ = UDP_BUFFER_SIZE;

    enum ClientState
    {
        kInitializing = 0,
        kWatingForServer,
        kConnected
    };
    ClientState state_ = kInitializing;

    /**
     * @brief Send magic word to server to establish connection.
     */
    void send_magic_word_()
    {
        // Convert sizeof(T_data) to bytes
        log_debug_("Sending magic word: " + std::string(kMagicWord) + " to server on port: " + std::to_string(port_) + " with IP: " + server_ip_);

        sendto(sockfd_, kMagicWord, strlen(kMagicWord),
               MSG_DONTWAIT, (const struct sockaddr *)&servaddr_,
               sizeof(servaddr_));
    }

    /**
     * @brief Receive data from the server.
     * 
     * @param data Reference to the data object where received data will be stored.
     * @return true If data is received successfully.
     * @return false If data is not received.
     */
    bool receive_data_(T_data &data)
    {
        int n;

        socklen_t len = sizeof(servaddr_);
        n = recvfrom(sockfd_, (char *)buffer_, buffer_size_,
                     MSG_WAITALL, (struct sockaddr *)&servaddr_,
                     &len);
        log_debug_("Received buffer size: " + std::to_string(n));
        if (n <= 0) // No data received
        {
            state_ = ClientState::kWatingForServer;
            return false;
        }
        buffer_[n] = '\0';

        // Check if received data is of correct size
        if (n != kSizeData)
        {
            log_error_("Received data size: " + std::to_string(n) + " is not equal to expected size: " + std::to_string(kSizeData));
            exit(EXIT_FAILURE);
            return false;
        }

        if (state_ == ClientState::kWatingForServer)
        {
            log_info_("Connected to server on port: " + std::to_string(port_) + " with IP: " + server_ip_);
            state_ = ClientState::kConnected;
        }

        // Copy data to output
        memcpy(&data, buffer_, kSizeData);
        return true;
    }

    /**
     * @brief Log informational messages.
     * 
     * @param info Informational message.
     */
    virtual void log_info_(const std::string &info)
    {
#if not INFO_PRINT_ON
        return;
#endif
        std::cout << info << std::endl;
    }

    /**
     * @brief Log error messages.
     * 
     * @param error Error message.
     */
    virtual void log_error_(const std::string &error)
    {
        std::cerr << error << std::endl;
        std::cerr.flush();
    }

    /**
     * @brief Log debug messages.
     * 
     * @param debug Debug message.
     */
    virtual void log_debug_(const std::string &debug)
    {
#if not DEBUG_PRINT_ON
        return;
#endif
        std::cout << debug << std::endl;
    }
};

#undef UDP_HANDSHAKE_MAGIC_WORD
#undef UDP_BUFFER_SIZE
#undef UDP_LOOP_SLEEP_US
#undef UDP_CLIENT_RECONNECT_TIMEOUT_MS
#undef INFO_PRINT_ON
#undef DEBUG_PRINT_ON
#endif // SIMPLESTRUCTCOMMS_HPP