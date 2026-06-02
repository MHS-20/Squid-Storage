#pragma once
#include <thread>
#include <string.h>
#include <iostream>

#include "filelock.hpp"
#include "filetransfer.hpp"
#include "squidprotocol.hpp"
#include "../networking/TCPConnectorChannel.hpp"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFFER_SIZE 1024

class Peer
{
public:
    Peer();
    Peer(std::string nodeType, std::string processName);
    Peer(int port, std::string nodeType, std::string processName);
    Peer(const char *server_ip, int port, std::string nodeType, std::string processName);
    virtual ~Peer();

    virtual void connectToServer();
    virtual void reconnect();

    virtual void run() = 0;
    virtual void handleRequest(const Message &msg);

protected:
    int port = SERVER_PORT;
    const char *server_ip = SERVER_IP;
    int timeoutSeconds = 60;

    std::string nodeType;
    std::string processName;

    FileLock file_lock;
    FileTransfer fileTransfer;
    SquidProtocol squidProtocol;
};
