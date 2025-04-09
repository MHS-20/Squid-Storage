#pragma once
#include "squidprotocolformatter.hpp"
#include <sys/socket.h>
#include "filetransfer.hpp"
#include "../../client/src/filemanager.hpp" // ! to be changed
#define BUFFER_SIZE 1024
#define DEFAULT_FOLDER_PATH "./test_txt"

class SquidProtocol
{
public:
    SquidProtocol();
    ~SquidProtocol();
    SquidProtocol(int socket_fd, std::string processName, std::string nodeType);
    virtual int getSocket();

    virtual Message receiveAndParseMessage();
    virtual std::string receiveMessageWithLength();

    virtual void requestDispatcher(Message request);
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

    virtual void response(bool lock);
    virtual void response(std::string ack);
    virtual void response(std::string nodeType, std::string processName);
    virtual void response(std::map<std::string, fs::file_time_type> filesLastWrite);

private:
    int socket_fd;
    std::string processName;
    std::string nodeType;
    char buffer[BUFFER_SIZE] = {0};

    FileTransfer fileTransfer;
    FileManager fileManager;
    SquidProtocolFormatter formatter;

    void sendMessage(std::string message);
    void transferFile(std::string filePath, Message response);
    void sendMessageWithLength(std::string &message);
};
