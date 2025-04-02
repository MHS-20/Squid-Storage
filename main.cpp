#include <iostream>
#include <thread>
#include "server.hpp"
#include "datanode.hpp"
#include "client.hpp"

using namespace std;
int main()
{
    std::thread server_thread([]()
                              {
        Server server(12345);
        server.start(); });

    std::this_thread::sleep_for(std::chrono::seconds(0));

    // std::thread datanode_thread1([]()
    //                             {
    // DataNode datanode("127.0.0.1", 12345);
    // datanode.connectToServer();
    // // datanode.sendMessage("Hello from datanode");
    // // datanode.receiveMessage();
    // datanode.sendFile("./test_txt/datanode.txt");
    // datanode.retriveFile("./test_txt/datatoserverfile.txt"); });

    // std::thread datanode_thread2([]()
    //                              {
    // DataNode datanode("127.0.0.1", 12345);
    // datanode.connectToServer();
    // std::this_thread::sleep_for(std::chrono::seconds(1));
    // // datanode.sendMessage("Hello from datanode");
    // // datanode.receiveMessage();
    // datanode.sendFile("./test_txt/datanode.txt");
    // datanode.retriveFile("./test_txt/datatoserverfile.txt"); });

    std::thread client_thread1([]()
                               {
    Client client("127.0.0.1", 12345);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    client.run(); });

    // std::thread client_thread2([]()
    //                            {
    // Client client("127.0.0.1", 12345);
    // client.connectToServer();
    // std::this_thread::sleep_for(std::chrono::seconds(3));
    // // client.sendMessage("Hello from client");
    // // client.receiveMessage();
    // client.run(); });
    client_thread1.join();
    std::cout << "Client thread finished" << std::endl;
    server_thread.join();

    return 0;
}