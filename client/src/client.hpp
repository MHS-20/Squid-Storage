#include <iostream>
#include <unistd.h>
#include <string.h>
#include <fstream>
#include <arpa/inet.h>
#include <thread>

#include "peer.hpp"
#include "filelock.hpp"
#include "filetransfer.hpp"
#include "squidprotocol.hpp"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFFER_SIZE 1024

class Client : public Peer
{
public:
    Client();
    Client(const char *server_ip, int port);
    Client(std::string nodeType, std::string processName);
    Client(const char *server_ip, int port, std::string nodeType, std::string processName);

    virtual void testing();
    virtual void run();
    virtual void initiateConnection();
    virtual void checkSecondarySocket();
    virtual void createFile(std::string filePath);
    virtual void createFile(std::string filePath, int version);
    virtual void deleteFile(std::string filePath);
    virtual void updateFile(std::string filePath);
    virtual void updateFile(std::string filePath, int version);
    virtual void syncStatus();
    virtual bool acquireLock(std::string filePath);
    virtual void releaseLock(std::string filePath);
    virtual bool isSecondarySocketAlive();

private:
    SquidProtocol secondarySquidProtocol;
};