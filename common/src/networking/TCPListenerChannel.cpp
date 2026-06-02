#include "TCPListenerChannel.hpp"

#include "TCPSquidChannel.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>

TCPListenerChannel::TCPListenerChannel(int port, int backlog)
{
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
        throw std::runtime_error("TCPListenerChannel: socket creation failed");

    int opt = 1;
    if (setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        close();
        throw std::runtime_error("TCPListenerChannel: setsockopt(SO_REUSEADDR) failed");
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        close();
        throw std::runtime_error(std::string("TCPListenerChannel: bind failed: ") + std::strerror(errno));
    }

    if (::listen(listenFd_, backlog) < 0)
    {
        close();
        throw std::runtime_error("TCPListenerChannel: listen failed");
    }
}

TCPListenerChannel::~TCPListenerChannel()
{
    close();
}

AcceptedConnection TCPListenerChannel::acceptConnection()
{
    AcceptedConnection accepted;
    if (!isOpen())
        return accepted;

    struct sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    int fd = ::accept(listenFd_, reinterpret_cast<sockaddr *>(&peer), &len);
    if (fd < 0)
        return accepted;

    char ip[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip)) != nullptr)
        accepted.peerIp = ip;
    accepted.peerPort = ntohs(peer.sin_port);
    accepted.channel = std::make_shared<TCPSquidChannel>(fd);
    return accepted;
}

std::optional<AcceptedConnection> TCPListenerChannel::waitForConnection(int timeoutSeconds)
{
    if (!isOpen())
        return std::nullopt;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(listenFd_, &readfds);

    struct timeval timeout{};
    timeout.tv_sec = timeoutSeconds;
    timeout.tv_usec = 0;

    int ready = ::select(listenFd_ + 1, &readfds, nullptr, nullptr, &timeout);
    if (ready < 0)
        return std::nullopt;
    if (ready == 0)
        return std::nullopt;
    if (!FD_ISSET(listenFd_, &readfds))
        return std::nullopt;

    AcceptedConnection accepted = acceptConnection();
    if (!accepted.channel)
        return std::nullopt;
    return accepted;
}

bool TCPListenerChannel::isOpen() const
{
    return listenFd_ >= 0;
}

void TCPListenerChannel::close()
{
    if (listenFd_ >= 0)
    {
        ::close(listenFd_);
        listenFd_ = -1;
    }
}
