#pragma once
#include <sys/socket.h>
#include <signal.h>
#include <thread>
#include "filetransfer.hpp"
#include "filemanager.hpp"
#include "squidProtocolFormatter.hpp"

#define BUFFER_SIZE 1024
#define DEFAULT_FOLDER_PATH fs::current_path().string() // current directory
using namespace std;

class SquidProtocol
{
public:
    SquidProtocol();
    ~SquidProtocol();
    SquidProtocol(int socket_fd, string nodeType, string processName);

    virtual bool isAlive();
    virtual void setIsAlive(bool isAlive);

    virtual int getSocket();
    virtual void setSocket(int socket_fd);

    virtual string getProcessName() const { return processName; }
    virtual string getNodeType() const { return nodeType; }
    virtual string toString() const;

    virtual Message receiveAndParseMessage();
    virtual string receiveMessageWithLength();
    bool handleErrors(ssize_t bytesRead);

    virtual void requestDispatcher(Message request);
    virtual void responseDispatcher(Message response);

    virtual Message identify();
    virtual Message connectServer();
    virtual Message closeConn();
    virtual Message listFiles();

    virtual Message createFile(string filePath);
    virtual Message createFile(string filePath, int version);
    virtual Message readFile(string filePath);
    virtual Message updateFile(string filePath);
    virtual Message updateFile(string filePath, int version);
    virtual Message deleteFile(string filePath);

    virtual Message acquireLock(string filePath);
    virtual Message releaseLock(string filePath);

    virtual Message heartbeat();
    virtual Message syncStatus();

    void sendMessage(string message);
    void transferFile(string filePath, Message response);
    void sendMessageWithLength(string &message);

    virtual void response(bool lock);
    virtual void response(int port);
    virtual void response(string ack);
    virtual void response(string nodeType, string processName);
    virtual void response(map<string, int> fileVersionMap);
    // deprecated
    virtual void response(map<string, fs::file_time_type> filesLastWrite);
    virtual void response(map<string, long long> fileTimeMap);

protected:
    int socket_fd;
    char buffer[BUFFER_SIZE] = {0};

    bool alive;
    string processName;
    string nodeType;

    FileTransfer fileTransfer;
    SquidProtocolFormatter formatter;
};
