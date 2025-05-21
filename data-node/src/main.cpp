#include <iostream>
#include <thread>
#include "datanode.hpp"

int main(int argc, char **argv)
{
    const char* server_ip = SERVER_IP;
    int server_port = SERVER_PORT;

    if (argc > 1)
        server_ip = argv[1];
    if (argc > 2)
        server_port = atoi(argv[2]);

    std::cout << "Starting DataNode. Server IP: " << server_ip << ", Server Port: " << server_port << std::endl;
    std::string currentPath = fs::current_path().string(); // current directory
    DataNode datanode(server_ip, server_port, std::string("DATANODE"), currentPath);
    datanode.run();

    return 0;
}