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
#define DEFAULT_PATH "./test_txt/test_server"
#define DEFAULT_REPLICATION_FACTOR 3

class Server
{
public:
    Server(int port);
    Server(int port, int replicationFactor);
    Server();
    ~Server();

    void run();
    //int getSocket();

    void buildFileLockMap();
    bool releaseLock(std::string path);
    bool acquireLock(std::string path);
    void identify(SquidProtocol clientProtocol);
    void createFileOnDataNodes(std::string filePath, SquidProtocol clientProtocol);
    void updateFileOnDataNodes(std::string filePath, SquidProtocol clientProtocol);
    void getFileFromDataNode(std::string filePath, SquidProtocol clientProtocol);
    void deleteFileFromDataNodes(std::string filePath, SquidProtocol clientProtocol);

private:
    int port;
    int opt = 1;
    char buffer[BUFFER_SIZE] = {0};

    int replicationFactor;
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    FileTransfer fileTransfer;

    std::map<std::string, FileLock> fileLockMap;
    std::map<std::string, long long> fileTimeMap;

    // std::map<std::string, FileLock> fileMap;
    std::map<std::string, SquidProtocol> clientEndpointMap;
    std::map<std::string, SquidProtocol> dataNodeEndpointMap;

    // maps filename to datanode holding that file (datanode, socket)
    std::map<std::string, std::map<std::string, SquidProtocol>> dataNodeReplicationMap;

    // iterators for round robin redundancy
    std::map<std::string, SquidProtocol>::iterator endpointIterator;
    std::map<std::string, SquidProtocol>::iterator readsLoadBalancingIterator;

    void handleConnection(int client_socket);
    void printMap(std::map<std::string, SquidProtocol> &map, std::string name);
    void printMap(std::map<std::string, FileLock> &map, std::string name);
    void printMap(std::map<std::string, std::map<std::string, SquidProtocol>> &map, std::string name);
    void printMap(std::map<std::string, long long> &map, std::string name);
};
