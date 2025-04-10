#pragma once
#include <sys/socket.h>
#include "filetransfer.hpp"
#include "filemanager.hpp"
#include "squidProtocolFormatter.hpp"

#define BUFFER_SIZE 1024
#define DEFAULT_FOLDER_PATH "./test_txt"

class SquidProtocolCommunicator
{
public:
    SquidProtocolCommunicator();
    SquidProtocolCommunicator(int socket_fd, std::string nodeType, std::string processName);

    virtual int getSocket();
    virtual Message receiveAndParseMessage();

    void sendMessage(std::string message);
    void transferFile(std::string filePath, Message response);

private:
    int socket_fd;
    std::string processName;
    std::string nodeType;
    char buffer[BUFFER_SIZE] = {0};

    FileTransfer fileTransfer;
    SquidProtocolFormatter formatter;

    void sendMessageWithLength(std::string &message);
    virtual std::string receiveMessageWithLength();

};
