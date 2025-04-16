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
    DataNode datanode = DataNode(port);
    datanode.run();
}