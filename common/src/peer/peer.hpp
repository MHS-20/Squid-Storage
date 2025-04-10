#pragma once
#include <iostream>
#include <unistd.h>
#include <string.h>
#include <fstream>
#include <arpa/inet.h>
#include <thread>

#include "filelock.hpp"
#include "filetransfer.hpp"
#include "squidprotocol.hpp"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

class Peer
{
public:
    Peer();
    Peer(std::string nodeType, std::string processName);
    Peer(const char *server_ip, int port, std::string nodeType, std::string processName);
    ~Peer();

    virtual void connectToServer();
    virtual int getSocket();
    
    //virtual void run();
    virtual void handleRequest(Message mex);

protected:
    int socket_fd = -1;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE] = {0};

    std::string nodeType;
    std::string processName;
    
    FileLock file_lock;
    FileTransfer fileTransfer;
    SquidProtocol squidProtocol;
};