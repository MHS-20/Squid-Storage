#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <fstream>

#include "filelock.hpp"
#include "filetransfer.hpp"
#include "squidprotocol.hpp"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

class DataNode
{

public:
    DataNode();
    DataNode(const char *server_ip, int port);
    ~DataNode();

    virtual void connectToServer();
    virtual int getSocket();
    
    virtual void run();
    virtual void handleRequest(Message mex);

    /* Messages for Testing */
    virtual void sendMessage(const char *message);
    virtual void receiveMessage();

    /* File Transfer API */
    void sendFile(const char *filepath);
    void retriveFile(const char *outputpath);

private:
    int socket_fd = 0;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE] = {0};
    
    FileLock file_lock;
    FileTransfer fileTransfer;
    SquidProtocol squidProtocol;

    void sendName(int socket_fd); 
};