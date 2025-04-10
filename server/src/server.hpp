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
#include "squidprotocol.hpp"

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024
#define DEFAULT_PATH "./test_txt"

class Server
{
public:
    Server(int port);
    Server();
    ~Server();

    virtual void start();
    virtual int getSocket();

private:
    int port;
    int opt = 1;
    char buffer[BUFFER_SIZE] = {0};

    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    FileTransfer fileTransfer;
    FileManager& fileManager;

    //std::map<std::string, FileLock> fileMap;             
    std::map<std::string, SquidProtocol> clientEndpointMap;   
    std::map<std::string, SquidProtocol> dataNodeEndpointMap;

    void handleConnection(int client_socket);
    // virtual bool replicateFileToDataNodes(int fileId, std::vector<int> ip);
    // virtual bool receiveFileFromDataNode(int fileId, int ip);
    // virtual bool sendFileToClient(int fileId, int ip);
    // virtual bool receiveFileFromClient(int fileId, int ip);
};
