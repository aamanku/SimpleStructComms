#include "SimpleStructCommsTwoWay.hpp"

struct A
{
	long long utc_time;
	unsigned long index;
	int data[100];

};

struct B
{
	long long utc_time;
	unsigned long index;
	float data[100];
};

int main()
{
	UDPComms<A, B> commsA("127.0.0.1", 8080, 8081, false, 1000);
	UDPComms<B, A> commsB("127.0.0.1", 8081, 8080, false, 1000);

	A dataA;
	B dataB;

	unsigned long indexA = 0;
	unsigned long indexB = 0;
	long long received_timeA = 0;
	long long received_timeB = 0;
	while (true)
	{
		{ // commsA
			// send
			dataA.index++;
			dataA.utc_time = std::chrono::system_clock::now().time_since_epoch().count();
			commsA.stream_data(dataA);
			// receive
			B dataB_received;
			commsA.get_data(dataB_received);
			std::cout << "[commsA] Received: " << dataB_received.index << std::endl;
			indexA = dataB_received.index;
			received_timeA = dataB_received.utc_time;
		}

		{ // commsB
			// send
			dataB.index++;
			dataB.utc_time = std::chrono::system_clock::now().time_since_epoch().count();
			commsB.stream_data(dataB);
			// receive
			A dataA_received;
			commsB.get_data(dataA_received);
			std::cout << "[commsB] Received: " << dataA_received.index << std::endl;
			indexB = dataA_received.index;
			received_timeB = dataA_received.utc_time;
		}

		if (indexA != indexB)
		{
			std::cout << "Index mismatch: " << indexA << " != " << indexB << std::endl;
		}

		std::cout<<"Difference: "<<(received_timeA - received_timeB)/1000.0<<" us"<<std::endl;

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

}

