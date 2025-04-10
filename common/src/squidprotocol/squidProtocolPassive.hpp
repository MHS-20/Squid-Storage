#pragma once
#include <sys/socket.h>

#include "filetransfer.hpp"
#include "filemanager.hpp"

#include "squidProtocolCommunicator.hpp"
#include "squidProtocolFormatter.hpp"

#define BUFFER_SIZE 1024
#define DEFAULT_FOLDER_PATH "./test_txt"

class SquidProtocolPassive
{
public:
    SquidProtocolPassive();
    SquidProtocolPassive(int socket_fd, std::string nodeType, std::string processName, SquidProtocolCommunicator communicator);

    void response(bool lock);
    void response(std::string ack);
    void response(std::string nodeType, std::string processName);
    void response(std::map<std::string, fs::file_time_type> filesLastWrite);

    void requestDispatcher(Message response);
    void responseIdentify();

private:
    int socket_fd;
    std::string processName;
    std::string nodeType;
    char buffer[BUFFER_SIZE] = {0};

    FileTransfer fileTransfer;
    SquidProtocolFormatter formatter;
    SquidProtocolCommunicator communicator;
};
