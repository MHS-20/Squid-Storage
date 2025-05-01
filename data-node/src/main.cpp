#include <iostream>
#include <thread>
#include "datanode.hpp"

int main(int argc, char **argv)
{
    int port = 12345;
    if (argc > 1)
    {
        port = atoi(argv[1]);
    }
    std::cout << "Starting datanode" << std::endl;
    std::string currentPath = fs::current_path().string(); // current directory
    DataNode datanode = DataNode(port, std::string("DATANODE"), currentPath);
    datanode.run();
}