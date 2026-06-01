#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "squidprotocol.hpp"

#define MOCK_CLIENT_SERVER_IP   "127.0.0.1"
#define MOCK_CLIENT_SERVER_PORT 12345
#define MOCK_CLIENT_SECONDARY_PORT 12346

class MockClient
{
public:
    MockClient(const char *serverIp, int serverPort, int secondaryPort,
               const std::string &processName);
    ~MockClient();

    void connect();
    void disconnect();

    Message createFile(const std::string &filePath, int version = 0);
    Message readFile  (const std::string &filePath);
    Message updateFile(const std::string &filePath, int version = 0);
    Message deleteFile(const std::string &filePath);
    Message acquireLock(const std::string &filePath);
    Message releaseLock(const std::string &filePath);
    Message syncStatus();
    Message heartbeat();

    void runPushListener();
    void stopPushListener();

    bool isAlive() const { return primaryProtocol_.isAlive(); }

private:
    const char *serverIp_;
    int         serverPort_;
    int         secondaryPort_;
    std::string processName_;

    int primaryFd_   = -1;
    int secondaryFd_ = -1;
    int listenFd_    = -1;

    SquidProtocol primaryProtocol_;
    SquidProtocol secondaryProtocol_;

    std::thread       pushThread_;
    std::atomic<bool> pushRunning_{false};

    void doHandshake();
    void openSecondaryListener();
    void acceptSecondaryConnection();
};
