#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <fstream>

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
    // virtual void run();

    /* Messages for Testing */
    virtual void sendMessage(const char *message);
    virtual void receiveMessage();

    /* File Transfer API */
    void sendFile(const char *filepath);
    void retriveFile(const char *outputpath);

    // virtual bool sendFileToServer(int fileId);
    // virtual bool receiveFileFromServer(int fileId);

private:
    int socket_fd = 0;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE] = {0};
    
    FileTransfer fileTransfer;
    SquidProtocol squidProtocol;
    void sendName(int socket_fd); 
};