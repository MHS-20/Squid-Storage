#include <iostream>
#include <thread>
#include "server.hpp"
#include "datanode.hpp"
#include "client.hpp"

using namespace std; 
int main() {
    std::thread server_thread([]() {
        Server server(12345);
        server.start();
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    DataNode datanode("127.0.0.1", 12345);
    datanode.connectToServer();
    // datanode.sendMessage("Hello from datanode");
    // datanode.receiveMessage();
    datanode.sendFile("./datanode.txt");
    datanode.retriveFile("datatoserverfile.txt");

    Client client("127.0.0.1", 12345);
    client.connectToServer();
    // client.sendMessage("Hello from client");
    // client.receiveMessage();
    client.sendFile("./clientfile.txt");
    client.retriveFile("clienttoserverfile.txt");

    return 0;
}