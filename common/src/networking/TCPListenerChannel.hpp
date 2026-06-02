#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "INetworkChannel.hpp"

struct AcceptedConnection
{
    std::shared_ptr<INetworkChannel> channel;
    std::string                     peerIp;
    uint16_t                        peerPort = 0;
};

class TCPListenerChannel
{
public:
    TCPListenerChannel(int port, int backlog = 3);
    ~TCPListenerChannel();

    AcceptedConnection acceptConnection();
    std::optional<AcceptedConnection> waitForConnection(int timeoutSeconds);
    bool               isOpen() const;
    void               close();

private:
    int listenFd_ = -1;
};
