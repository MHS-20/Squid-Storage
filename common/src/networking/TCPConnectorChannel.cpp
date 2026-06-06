#include "TCPConnectorChannel.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

TCPConnectorChannel::TCPConnectorChannel(const std::string &host, int port,
                                         int timeoutSeconds,
                                         int retryDelaySeconds) {
  (void)retryDelaySeconds; // retry is the caller's responsibility

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
    throw std::runtime_error("TCPConnectorChannel: invalid address");

  socketFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socketFd_ < 0)
    throw std::runtime_error("TCPConnectorChannel: socket() failed");

  // Non-blocking connect with select() timeout.
  int flags = ::fcntl(socketFd_, F_GETFL, 0);
  ::fcntl(socketFd_, F_SETFL, flags | O_NONBLOCK);

  int rc =
      ::connect(socketFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  if (rc != 0 && errno != EINPROGRESS) {
    ::close(socketFd_);
    socketFd_ = -1;
    throw std::runtime_error(
        std::string("TCPConnectorChannel: connect failed: ") + strerror(errno));
  }

  if (errno == EINPROGRESS) {
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(socketFd_, &wset);
    struct timeval tv{timeoutSeconds, 0};
    int ready = ::select(socketFd_ + 1, nullptr, &wset, nullptr, &tv);
    if (ready <= 0) {
      ::close(socketFd_);
      socketFd_ = -1;
      throw std::runtime_error("TCPConnectorChannel: connect timed out");
    }
    int err = 0;
    socklen_t len = sizeof(err);
    ::getsockopt(socketFd_, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
      ::close(socketFd_);
      socketFd_ = -1;
      throw std::runtime_error(
          std::string("TCPConnectorChannel: connect error: ") + strerror(err));
    }
  }

  // Restore blocking mode and set recv/send timeouts.
  flags = ::fcntl(socketFd_, F_GETFL, 0);
  ::fcntl(socketFd_, F_SETFL, flags & ~O_NONBLOCK);
  struct timeval tv{timeoutSeconds, 0};
  ::setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(socketFd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

TCPConnectorChannel::~TCPConnectorChannel() { close(); }

ssize_t TCPConnectorChannel::readBytes(uint8_t *buf, size_t len) {
  if (!isOpen())
    return -1;
  return ::recv(socketFd_, buf, len, 0);
}

ssize_t TCPConnectorChannel::writeBytes(const uint8_t *buf, size_t len) {
  if (!isOpen())
    return -1;
  return ::send(socketFd_, buf, len, 0);
}

int TCPConnectorChannel::getSocket() const { return socketFd_; }

bool TCPConnectorChannel::isOpen() const { return socketFd_ >= 0; }

void TCPConnectorChannel::close() {
  if (socketFd_ >= 0) {
    ::close(socketFd_);
    socketFd_ = -1;
  }
}
