#include "squidprotocolformatter.hpp"
#include <sys/socket.h>
#include "filetransfer.hpp"
#define BUFFER_SIZE 1024

class SquidProtocol
{
public:
    SquidProtocol(int socket_fd, std::string processName);
    ~SquidProtocol();
    virtual std::string createFile(std::string filePath);
    virtual std::string transferFile(std::string fileContent);
    virtual std::string readFile(std::string filePath);
    virtual std::string updateFile(std::string filePath);
    virtual std::string deleteFile(std::string filePath);
    virtual bool acquireLock(std::string filePath);
    virtual std::string releaseLock(std::string filePath);
    virtual std::string heartbeat();
    virtual std::string syncStatus();
    virtual void identify();
    virtual void response(std::string ack);
    virtual void response(std::string nodeType, std::string processName);
    virtual void response(bool lock);

private:
    int socket_fd;
    std::string processName;
    char buffer[BUFFER_SIZE] = {0};
    FileTransfer fileTransfer;
    SquidProtocolFormatter formatter;
    void sendMessage(std::string message);
    Message receiveAndParseMessage();
    std::string receive();
};
