#include <vector>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fstream>
#include <map>

#include "filetransfer.hpp"
#include "squidprotocol.hpp"

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024

class Server
{
public:
    void start();
    Server(int port);
    Server();
    ~Server();

    int getSocket();

    void receiveFile(int client_socket, const char *outputpath);
    void sendFile(int client_socket, const char *filepath);
    // virtual bool listenForClients();
    // virtual bool listenForDataNodes();
    // virtual bool lock(int filedId);
    // virtual bool unlock(int fileId);
    // virtual bool updateFile(int fileId);
    // virtual bool deleteFile(int fileId);
    // virtual bool createFile(int fileId);

private:
    int port;
    int opt = 1;
    char buffer[BUFFER_SIZE] = {0};

    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    SquidProtocol squidProtocol;
    FileTransfer fileTransfer;

    std::map<std::string, int> fileMap;             // filename to file id
    std::map<std::string, int> clientEndpointMap;   // client ip/name to socket_fd
    std::map<std::string, int> dataNodeEndpointMap; // datanode ip/name to socket_fd

    void identifyConnection(int client_socket);
    void handleClient(int client_socket);
    void handleClientMessage(int client_socket);
    // virtual bool replicateFileToDataNodes(int fileId, std::vector<int> ip);
    // virtual bool receiveFileFromDataNode(int fileId, int ip);
    // virtual bool sendFileToClient(int fileId, int ip);
    // virtual bool receiveFileFromClient(int fileId, int ip);
};
