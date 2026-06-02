#include "TCPConnectorChannel.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

TCPConnectorChannel::TCPConnectorChannel(const std::string &host, int port,
                                         int timeoutSeconds,
                                         int retryDelaySeconds)
{
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
        throw std::runtime_error("TCPConnectorChannel: invalid remote address");

    while (socketFd_ < 0)
    {
        socketFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socketFd_ < 0)
            throw std::runtime_error("TCPConnectorChannel: socket creation failed");

        struct timeval timeout{};
        timeout.tv_sec = timeoutSeconds;
        timeout.tv_usec = 0;

        if (setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO,
                       (const char *)&timeout, sizeof(timeout)) < 0)
        {
            ::close(socketFd_);
            socketFd_ = -1;
            throw std::runtime_error("TCPConnectorChannel: setsockopt(SO_RCVTIMEO) failed");
        }

        if (setsockopt(socketFd_, SOL_SOCKET, SO_SNDTIMEO,
                       (const char *)&timeout, sizeof(timeout)) < 0)
        {
            ::close(socketFd_);
            socketFd_ = -1;
            throw std::runtime_error("TCPConnectorChannel: setsockopt(SO_SNDTIMEO) failed");
        }

        if (::connect(socketFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0)
            break;

        ::close(socketFd_);
        socketFd_ = -1;
        std::cerr << "TCPConnectorChannel: connect failed, retrying..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(retryDelaySeconds));
    }
}

TCPConnectorChannel::~TCPConnectorChannel()
{
    close();
}

ssize_t TCPConnectorChannel::readBytes(uint8_t *buf, size_t len)
{
    if (!isOpen())
        return -1;
    return ::recv(socketFd_, buf, len, 0);
}

ssize_t TCPConnectorChannel::writeBytes(const uint8_t *buf, size_t len)
{
    if (!isOpen())
        return -1;
    return ::send(socketFd_, buf, len, 0);
}

int TCPConnectorChannel::getSocket() const
{
    return socketFd_;
}

bool TCPConnectorChannel::isOpen() const
{
    return socketFd_ >= 0;
}

void TCPConnectorChannel::close()
{
    if (socketFd_ >= 0)
    {
        ::close(socketFd_);
        socketFd_ = -1;
    }
}
