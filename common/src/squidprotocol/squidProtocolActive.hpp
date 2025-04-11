#pragma once
#include <sys/socket.h>

#include "filetransfer.hpp"
#include "filemanager.hpp"


#include "squidProtocolCommunicator.hpp"
#include "squidProtocolFormatter.hpp"

#define BUFFER_SIZE 1024
#define DEFAULT_FOLDER_PATH "./test_txt"

// Requester
class SquidProtocolActive
{
public:
    SquidProtocolActive();
    SquidProtocolActive(int socket_fd, std::string nodeType, std::string processName, SquidProtocolCommunicator communicator);
    
    virtual void responseDispatcher(Message response);

    virtual Message identify();
    virtual Message closeConn();

    virtual Message createFile(std::string filePath);
    virtual Message readFile(std::string filePath);
    virtual Message updateFile(std::string filePath);
    virtual Message deleteFile(std::string filePath);

    virtual Message acquireLock(std::string filePath);
    virtual Message releaseLock(std::string filePath);

    virtual Message heartbeat();
    virtual Message syncStatus();

protected:
    int socket_fd;
    std::string processName;
    std::string nodeType;
    char buffer[BUFFER_SIZE] = {0};

    FileTransfer fileTransfer;
    SquidProtocolFormatter formatter;
    SquidProtocolCommunicator communicator;
};
