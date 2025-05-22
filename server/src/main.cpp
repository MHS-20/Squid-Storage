#include <iostream>
#include <thread>
#include "server.hpp"

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT;
    int replicationFactor = DEFAULT_REPLICATION_FACTOR;
    int timeoutSeconds = DEFAULT_TIMEOUT;

    if (argc > 1)
        port = atoi(argv[1]);
    if (argc > 2)
        replicationFactor = atoi(argv[2]);
    if (argc > 3)
        timeoutSeconds = atoi(argv[3]);

    std::cout << "Starting server on port: " << port
              << ", replication factor: " << replicationFactor
              << ", timeout: " << timeoutSeconds << "s" << std::endl;

    Server server(port, replicationFactor, timeoutSeconds);
    server.run();
}