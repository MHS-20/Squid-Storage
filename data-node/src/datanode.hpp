#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <fstream>
#include <thread>

#include "../../common/src/peer/peer.hpp"
#include "filelock.hpp"
#include "filetransfer.hpp"
#include "squidprotocol.hpp"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

class DataNode: public Peer
{

public:
    DataNode();
    DataNode(int port);
    DataNode(const char *server_ip, int port);
    DataNode(std::string nodeType, std::string processName);
    DataNode(const char *server_ip, int port, std::string nodeType, std::string processName);
    
    void run() override;
    void testing();
    //void handleRequest(Message mex);

private:
    // int socket_fd = -1;
    // struct sockaddr_in server_addr;
    // char buffer[BUFFER_SIZE] = {0};
    
    // FileLock file_lock;
    // FileTransfer fileTransfer;
    // SquidProtocol squidProtocol;
};