#include "client.hpp"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

MockClient::MockClient(const char *serverIp, int serverPort, int secondaryPort,
                       const std::string &processName)
    : serverIp_(serverIp), serverPort_(serverPort),
      secondaryPort_(secondaryPort), processName_(processName),
      primaryProtocol_(), secondaryProtocol_() {}

MockClient::~MockClient() {
  stopPushListener();
  if (primaryFd_ >= 0)
    ::close(primaryFd_);
  if (secondaryFd_ >= 0)
    ::close(secondaryFd_);
  if (listenFd_ >= 0)
    ::close(listenFd_);
}

void MockClient::openSecondaryListener() {
  listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd_ < 0)
    throw std::runtime_error("MockClient: secondary listen socket failed");

  int opt = 1;
  setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(secondaryPort_));

  if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    throw std::runtime_error(std::string("MockClient: bind secondary port: ") +
                             std::strerror(errno));

  if (::listen(listenFd_, 1) < 0)
    throw std::runtime_error("MockClient: listen secondary failed");
}

void MockClient::acceptSecondaryConnection() {
  sockaddr_in peer{};
  socklen_t len = sizeof(peer);
  secondaryFd_ = ::accept(listenFd_, reinterpret_cast<sockaddr *>(&peer), &len);
  if (secondaryFd_ < 0)
    throw std::runtime_error("MockClient: accept secondary failed");

  secondaryProtocol_ = SquidProtocol(secondaryFd_, "CLIENT", processName_);
  std::cout << "[MockClient]: Secondary connection accepted" << std::endl;
}

void MockClient::doHandshake() {
  Message identify = primaryProtocol_.receiveAndParse();
  if (identify.opcode != Opcode::IDENTIFY)
    throw std::runtime_error("MockClient: expected IDENTIFY, got " +
                             identify.toString());

  primaryProtocol_.response(std::string("CLIENT"), processName_);

  Message ack = primaryProtocol_.receiveAndParse();
  if (!ack.isAck())
    throw std::runtime_error("MockClient: handshake ACK not received");

  std::cout << "[MockClient]: Identity acknowledged by server" << std::endl;

  Message connectReq = primaryProtocol_.receiveAndParse();
  if (connectReq.opcode != Opcode::CONNECT)
    throw std::runtime_error("MockClient: expected CONNECT, got " +
                             connectReq.toString());

  primaryProtocol_.response(secondaryPort_);
  std::cout << "[MockClient]: Sent secondary port " << secondaryPort_
            << " to server" << std::endl;

  acceptSecondaryConnection();
}

void MockClient::connect() {
  primaryFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (primaryFd_ < 0)
    throw std::runtime_error("MockClient: socket failed");

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(serverPort_));
  if (inet_pton(AF_INET, serverIp_, &addr.sin_addr) <= 0)
    throw std::runtime_error("MockClient: invalid server IP");

  while (::connect(primaryFd_, reinterpret_cast<sockaddr *>(&addr),
                   sizeof(addr)) < 0) {
    std::cerr << "[MockClient]: connect failed, retrying..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  std::cout << "[MockClient]: Primary connection established" << std::endl;

  primaryProtocol_ = SquidProtocol(primaryFd_, "CLIENT", processName_);

  openSecondaryListener();
  doHandshake();

  ::close(listenFd_);
  listenFd_ = -1;
}

void MockClient::disconnect() {
  stopPushListener();
  primaryProtocol_.closeConn();
}

Message MockClient::createFile(const std::string &filePath, int version) {
  return version > 0 ? primaryProtocol_.createFile(filePath, version)
                     : primaryProtocol_.createFile(filePath);
}

Message MockClient::readFile(const std::string &filePath) {
  return primaryProtocol_.readFile(filePath);
}

Message MockClient::updateFile(const std::string &filePath, int version) {
  return version > 0 ? primaryProtocol_.updateFile(filePath, version)
                     : primaryProtocol_.updateFile(filePath);
}

Message MockClient::deleteFile(const std::string &filePath) {
  return primaryProtocol_.deleteFile(filePath);
}

Message MockClient::acquireLock(const std::string &filePath) {
  return primaryProtocol_.acquireLock(filePath);
}

Message MockClient::releaseLock(const std::string &filePath) {
  return primaryProtocol_.releaseLock(filePath);
}

Message MockClient::syncStatus() { return primaryProtocol_.syncStatus(); }

Message MockClient::heartbeat() { return primaryProtocol_.heartbeat(); }

void MockClient::runPushListener() {
  pushRunning_ = true;
  pushThread_ = std::thread([this]() {
    std::cout << "[MockClient]: Push listener started" << std::endl;
    while (pushRunning_ && secondaryProtocol_.isAlive()) {
      Message msg = secondaryProtocol_.receiveAndParse();
      if (!secondaryProtocol_.isAlive())
        break;

      std::cout << "[MockClient]: Push received: " << msg.toString()
                << std::endl;
      secondaryProtocol_.requestDispatcher(msg);
    }
    std::cout << "[MockClient]: Push listener stopped" << std::endl;
  });
}

void MockClient::stopPushListener() {
  pushRunning_ = false;
  if (pushThread_.joinable())
    pushThread_.join();
}
