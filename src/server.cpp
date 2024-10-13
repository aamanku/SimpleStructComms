#include "SimpleStructComms.hpp"

int main(int, char**){

    UDPServer<int> server(8080, 1000);

    int a = 0;

    while(true)
    {
        server.set_data(a);
        a++;
        std::cout<<"Sent: "<<a<<std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

}
