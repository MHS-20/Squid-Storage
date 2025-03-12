#include <vector>
#include <netinet/in.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024

class Server {
    public: 
        void start();
        Server(int port);
        Server();
        ~Server();
        // virtual bool listenForClients();
        // virtual bool listenForDataNodes();
        // virtual bool lock(int filedId);
        // virtual bool unlock(int fileId);
        // virtual bool updateFile(int fileId);
        // virtual bool deleteFile(int fileId);
        // virtual bool createFile(int fileId);

    private: 
        int server_fd, new_socket;
        struct sockaddr_in address;
        int port;
        int opt = 1;
        socklen_t addrlen = sizeof(address);
        char buffer[BUFFER_SIZE] = {0};
        void handleClient(int client_socket);
        // virtual bool replicateFileToDataNodes(int fileId, std::vector<int> ip);
        // virtual bool receiveFileFromDataNode(int fileId, int ip);
        // virtual bool sendFileToClient(int fileId, int ip);
        // virtual bool receiveFileFromClient(int fileId, int ip);
};
