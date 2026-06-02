#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

class INetworkChannel
{
public:
    virtual ~INetworkChannel() = default;

    virtual ssize_t readBytes(uint8_t *buf, size_t len) = 0;
    virtual ssize_t writeBytes(const uint8_t *buf, size_t len) = 0;
    virtual int     getSocket() const = 0;
    virtual bool    isOpen()    const = 0;
    virtual void    close()           = 0;
};
