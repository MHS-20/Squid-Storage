#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "networking/TCPConnectorChannel.hpp"
#include "server_runtime.hpp"
#include "squidprotocol.hpp"

#define DEFAULT_SERVER_IP   "127.0.0.1"
#define DEFAULT_SERVER_PORT 12345

class Client
{
public:
    using PushHandler = std::function<void(const Message &)>;

    Client(const std::string &serverIp, int serverPort, const std::string &processName);
    ~Client();

    void connect();
    void disconnect();
    void setPushHandler(PushHandler handler);

    bool isAlive() const;

    Message createFile(const std::string &filePath, const std::vector<uint8_t> &data, int version = 0);
    Message readFile(const std::string &filePath, std::vector<uint8_t> &dataOut);
    Message updateFile(const std::string &filePath, const std::vector<uint8_t> &data, int version = 0);
    Message deleteFile(const std::string &filePath);
    Message acquireLock(const std::string &filePath);
    Message releaseLock(const std::string &filePath);
    Message syncStatus();
    Message heartbeat();

private:
    std::string serverIp_;
    int serverPort_;
    std::string processName_;

    PushHandler pushHandler_;

    std::shared_ptr<ConnectionSession> session_;

    void handlePush(ConnectionSession &session, const Message &message);
    void doHandshake(SquidProtocol &proto);
};
