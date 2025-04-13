#include <vector>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fstream>
#include <map>
#include <thread>

#include "filelock.hpp"
#include "filemanager.hpp"
#include "filetransfer.hpp"
#include "squidProtocolServer.cpp"

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024
#define DEFAULT_PATH "../../test_txt"
#define DEFAULT_REPLICATION_FACTOR 3

class Server
{
public:
    Server(int port);
    Server(int port, int replicationFactor);
    Server();
    ~Server();

    void run();
    int getSocket();
    void identify(SquidProtocolServer protocol);

private:
    int port;
    int opt = 1;
    char buffer[BUFFER_SIZE] = {0};

    int replicationFactor;
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    FileTransfer fileTransfer;
    FileManager &fileManager;

    // std::map<std::string, FileLock> fileMap;
    std::map<std::string, SquidProtocolServer> clientEndpointMap;
    std::map<std::string, SquidProtocolServer> dataNodeEndpointMap;

    // maps filename to datanode endpoint map holding that file
    std::map<std::string, std::map<std::string, SquidProtocolServer>> dataNodeReplicationMap;

    void handleConnection(int client_socket);
    // virtual bool replicateFileToDataNodes(int fileId, std::vector<int> ip);
};
