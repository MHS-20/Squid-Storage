#include <iostream>
#include <thread>
#include "server.hpp"
#include "datanode.hpp"

using namespace std; 
int main() {
    std::thread server_thread([]() {
        Server server(12345);
        server.start();
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    DataNode datanode("127.0.0.1", 12345);
    datanode.connectToServer();
    datanode.sendMessage("Hello from client");
    datanode.receiveMessage();

    server_thread.join();
    return 0;
}
