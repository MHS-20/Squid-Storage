#include <iostream>
#include <thread>
#include "server.hpp"

int main(int argc, char **argv)
{
    int port = 12345;
    if (argc > 1)
    {
        port = atoi(argv[1]);
    }
    std::cout << "Starting server on port: " << port << std::endl;
    Server server = Server(port);
    server.run();
}