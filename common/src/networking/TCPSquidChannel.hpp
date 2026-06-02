#pragma once

#include <memory>
#include "INetworkChannel.hpp"

class TCPSquidChannel : public INetworkChannel
{
public:
    explicit TCPSquidChannel(int socketFd = -1);
    ~TCPSquidChannel() override;

    ssize_t readBytes(uint8_t *buf, size_t len) override;
    ssize_t writeBytes(const uint8_t *buf, size_t len) override;
    int     getSocket() const override;
    bool    isOpen()    const override;
    void    close()           override;

private:
    int socketFd_ = -1;
};
