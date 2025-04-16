#pragma once
#include <sys/socket.h>

#include "filetransfer.hpp"
#include "filemanager.hpp"
#include "squidProtocolFormatter.hpp"

#define BUFFER_SIZE 1024
#define DEFAULT_FOLDER_PATH fs::current_path().string() // current directory

class SquidProtocol
{
public:
    SquidProtocol();
    ~SquidProtocol();
    SquidProtocol(int socket_fd, std::string nodeType, std::string processName);

    virtual bool isAlive();
    virtual int getSocket();
    virtual std::string toString() const;

    virtual Message receiveAndParseMessage();
    virtual std::string receiveMessageWithLength();

    virtual void requestDispatcher(Message request);
    virtual void responseDispatcher(Message response);

    virtual Message identify();
    virtual Message closeConn();
    virtual Message listFiles();

    virtual Message createFile(std::string filePath);
    virtual Message readFile(std::string filePath);
    virtual Message updateFile(std::string filePath);
    virtual Message deleteFile(std::string filePath);

    virtual Message acquireLock(std::string filePath);
    virtual Message releaseLock(std::string filePath);

    virtual Message heartbeat();
    virtual Message syncStatus();

    void sendMessage(std::string message);
    void transferFile(std::string filePath, Message response);
    void sendMessageWithLength(std::string &message);

    virtual void response(bool lock);
    virtual void response(std::string ack);
    virtual void response(std::string nodeType, std::string processName);
    virtual void response(std::map<std::string, fs::file_time_type> filesLastWrite);
    virtual void response(std::map<std::string, long long> fileTimeMap);

protected:
    int socket_fd;
    char buffer[BUFFER_SIZE] = {0};

    bool alive;
    std::string processName;
    std::string nodeType;

    FileTransfer fileTransfer;
    SquidProtocolFormatter formatter;
};
