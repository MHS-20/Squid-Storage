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
    datanode.sendFile("./test_txt/datanode.txt");
    datanode.retriveFile("./test_txt/datatoserverfile.txt");

    Client client("127.0.0.1", 12345);
    client.connectToServer();
    // client.sendMessage("Hello from client");
    // client.receiveMessage();
    client.sendFile("./test_txt/clientfile.txt");
    client.retriveFile("./test_txt/clienttoserverfile.txt");

    return 0;
}