#include "SimpleStructComms.hpp"

int main()
{
    UDPClient<int> client("127.0.0.1", 8080, 1000);
    int data = 0;
    while (true)
    {
        client.get_data(data);
        std::cout << "Received: " << data << std::endl;
    }
}
