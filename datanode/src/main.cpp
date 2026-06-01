#include <iostream>
#include <thread>
#include <string>
#include <filesystem>
#include "datanode.hpp"

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    const char* server_ip = SERVER_IP;
    int server_port = SERVER_PORT;
    std::string node_identity = "";

    if (argc > 1)
        server_ip = argv[1];
    if (argc > 2)
        server_port = atoi(argv[2]);
    if (argc > 3)
        node_identity = argv[3]; // Direct override via parameter!

    // Fall back to folder name if no explicit string argument was given
    if (node_identity.empty()) {
        node_identity = fs::current_path().filename().string(); 
    }

    std::cout << "Starting DataNode [" << node_identity 
              << "] -> Connecting to Server: " << server_ip 
              << ":" << server_port << std::endl;

    DataNode datanode(server_ip, server_port, std::string("DATANODE"), node_identity);
    datanode.run();

    return 0;
}