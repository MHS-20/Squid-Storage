#pragma once

#include <string>

#include "INetworkChannel.hpp"

class TCPConnectorChannel : public INetworkChannel
{
public:
    TCPConnectorChannel(const std::string &host, int port,
                        int timeoutSeconds = 60,
                        int retryDelaySeconds = 2);
    ~TCPConnectorChannel() override;

    ssize_t readBytes(uint8_t *buf, size_t len) override;
    ssize_t writeBytes(const uint8_t *buf, size_t len) override;
    int     getSocket() const override;
    bool    isOpen()    const override;
    void    close()           override;

private:
    int socketFd_ = -1;
};
