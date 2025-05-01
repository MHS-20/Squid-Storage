#pragma once
#include <thread>
#include <iostream>
#include <unistd.h>
#include <string.h>
#include <fstream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "filelock.hpp"
#include "filetransfer.hpp"
#include "squidprotocol.hpp"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024
using namespace std;

class Peer
{
public:
    Peer();
    Peer(string nodeType, string processName);
    Peer(int port, string nodeType, string processName);
    Peer(const char *server_ip, int port, string nodeType, string processName);
    ~Peer();

    virtual void connectToServer();
    virtual void reconnect();

    virtual int getSocket();

    virtual void run() = 0;
    virtual void handleRequest(Message mex);

protected:
    int port;
    int socket_fd = -1;
    const char *server_ip;
    int timeoutSeconds = 60;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE] = {0};

    string nodeType;
    string processName;

    FileLock file_lock;
    FileTransfer fileTransfer;
    SquidProtocol squidProtocol;
};