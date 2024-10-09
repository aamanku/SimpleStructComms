#include "SimpleStructComms.hpp"

struct __attribute__((packed)) timing
{
    // save utc time
    long long utc_time;
    unsigned long index;
    int data[100];

};


void server_threaded()
{
    
    UDPServer<timing> server(8080, 1000);
    server.start_server_thread();

    timing t;

    t.index = 0;

    while(true)
    {
        t.utc_time = std::chrono::system_clock::now().time_since_epoch().count();
        server.set_data(t);
        t.index++;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

}

void server()
{
    UDPServer<timing> server(8080, 1000);
    timing t;

    t.index = 0;

    while(true)
    {
        t.utc_time = std::chrono::system_clock::now().time_since_epoch().count();
        server.send_data(t);
        t.index++;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

}

void client()
{
    UDPClient<timing> client("127.0.0.1", 8080, 1000);
    timing data;
    unsigned long last_index = 0;
    double avg_delay = 0;
    unsigned long count = 0;
    while (true)
    {
        client.get_data(data);
        long long now = std::chrono::system_clock::now().time_since_epoch().count();
        auto diff = now - data.utc_time; 
        if(data.index != last_index + 1 && last_index != 0)
        {
            std::cout << "Missed: " << last_index << " to " << data.index << std::endl;
            throw std::runtime_error("Missed data");
        }
        last_index = data.index;
        // std::cout << "Delay: " << diff/1000.0 << " us, Index= "<< last_index << std::endl;
        count++;
        avg_delay = (avg_delay * (count - 1) + diff) / count;


        // print every 1000th data
        if(data.index % 1000 == 0)
        {
            std::cout << "Average delay: " << avg_delay << " ns" << std::endl;
        }
    }
}

int main()
{
    std::thread server_thread(server_threaded);
    // std::thread server_thread(server,0);
    // std::thread client_thread(client);
client();
    server_thread.join();
    // client_thread.join();

    return 0;
}