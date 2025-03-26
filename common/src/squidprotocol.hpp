#pragma once
#include "squidprotocolformatter.hpp"
#include <sys/socket.h>
#include "filetransfer.hpp"
#include "../../client/src/filemanager.hpp" // to be changed
#define BUFFER_SIZE 1024
#define DEFAULT_FOLDER_PATH "./data"

class SquidProtocol
{
public:
    SquidProtocol();
    SquidProtocol(int socket_fd, std::string processName, std::string nodeType);
    ~SquidProtocol();
    virtual std::string createFile(std::string filePath);
    virtual std::string readFile(std::string filePath);
    virtual std::string updateFile(std::string filePath);
    virtual std::string deleteFile(std::string filePath);
    virtual bool acquireLock(std::string filePath);
    virtual std::string releaseLock(std::string filePath);
    virtual std::string heartbeat();
    virtual std::string syncStatus();
    virtual Message identify();
    virtual void response(std::string ack);
    virtual void response(std::string nodeType, std::string processName);
    virtual void response(std::map<std::string, fs::file_time_type> filesLastWrite);
    virtual void response(bool lock);
    virtual void requestDispatcher(Message message);
    virtual Message receiveAndParseMessage();

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
    // std::string receive();
};
