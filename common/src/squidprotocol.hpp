#include "squidprotocolformatter.hpp"

class SquidProtocol
{
public:
    SquidProtocol();
    ~SquidProtocol();
    virtual void createFile(std::string filePath);
    virtual void transferFile(std::string fileContent);
    virtual void readFile(std::string filePath);
    virtual void updateFile(std::string filePath);
    virtual void deleteFile(std::string filePath);
    virtual void acquireLock(std::string filePath);
    virtual void releaseLock(std::string filePath);
    virtual void heartbeat();
    virtual void syncStatus();
    virtual void identify();
    virtual void response(std::string ack);
    virtual void response(std::string nodeType, std::string processName);
    virtual void response(bool lock);

private:
    SquidProtocolFormatter formatter;
    void send(std::string message);
    std::string receive();
};
