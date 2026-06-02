#include "TCPSquidChannel.hpp"
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

TCPSquidChannel::TCPSquidChannel(int socketFd)
    : socketFd_(socketFd)
{
}

TCPSquidChannel::~TCPSquidChannel()
{
    close();
}

ssize_t TCPSquidChannel::readBytes(uint8_t *buf, size_t len)
{
    if (!isOpen())
        return -1;
    return ::recv(socketFd_, buf, len, 0);
}

ssize_t TCPSquidChannel::writeBytes(const uint8_t *buf, size_t len)
{
    if (!isOpen())
        return -1;
    return ::send(socketFd_, buf, len, 0);
}

int TCPSquidChannel::getSocket() const
{
    return socketFd_;
}

bool TCPSquidChannel::isOpen() const
{
    return socketFd_ >= 0;
}

void TCPSquidChannel::close()
{
    if (socketFd_ >= 0)
    {
        ::close(socketFd_);
        socketFd_ = -1;
    }
}
