#include <iostream>
#include <thread>
#include "server.hpp"
#include "datanode.hpp"
#include "client.hpp"

using namespace std;
int main()
{
    std::thread server_thread([](){
        Server server(12345);
        server.run(); });

    std::this_thread::sleep_for(std::chrono::seconds(0));

    // std::thread datanode_thread1([](){
    // DataNode datanode("127.0.0.1", 12345);
    // std::this_thread::sleep_for(std::chrono::seconds(1));
    // datanode.run(); });

    std::thread client_thread1([](){
    Client client("127.0.0.1", 12345);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    client.testing(); });

    client_thread1.join();
    std::cout << "Client thread finished" << std::endl;

    // datanode_thread1.join();
    // std::cout << "Datanode thread finished" << std::endl;

    return 0;
}