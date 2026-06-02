#include "client.hpp"
#include <iostream>
#include <stdexcept>

MockClient::MockClient(const char *serverIp, int serverPort, int secondaryPort,
                       const std::string &processName)
    : serverIp_(serverIp), serverPort_(serverPort),
      secondaryPort_(secondaryPort), processName_(processName),
      primaryProtocol_(), secondaryProtocol_() {}

MockClient::~MockClient() {
  stopPushListener();
  if (secondaryProtocol_.isAlive())
    secondaryProtocol_.closeConn();
  if (primaryProtocol_.isAlive())
    primaryProtocol_.closeConn();
  if (secondaryListener_)
    secondaryListener_->close();
}

void MockClient::openSecondaryListener() {
  secondaryListener_ = std::make_unique<TCPListenerChannel>(secondaryPort_, 1);
}

void MockClient::acceptSecondaryConnection() {
  if (!secondaryListener_)
    throw std::runtime_error("MockClient: secondary listener not ready");

  AcceptedConnection accepted = secondaryListener_->acceptConnection();
  if (!accepted.channel)
    throw std::runtime_error("MockClient: accept secondary failed");

  secondaryProtocol_ = SquidProtocol(accepted.channel, "CLIENT", processName_);
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
  auto primaryChannel = std::make_shared<TCPConnectorChannel>(serverIp_, serverPort_, 60, 2);
  std::cout << "[MockClient]: Primary connection established" << std::endl;

  primaryProtocol_ = SquidProtocol(primaryChannel, "CLIENT", processName_);

  openSecondaryListener();
  doHandshake();

  if (secondaryListener_)
    secondaryListener_->close();
}

void MockClient::disconnect() {
  stopPushListener();
  primaryProtocol_.closeConn();
  if (secondaryProtocol_.isAlive())
    secondaryProtocol_.closeConn();
  if (secondaryListener_)
    secondaryListener_->close();
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
      secondaryProtocol_.responseDispatcher(msg);
    }
    std::cout << "[MockClient]: Push listener stopped" << std::endl;
  });
}

void MockClient::stopPushListener() {
  pushRunning_ = false;
  if (pushThread_.joinable())
    pushThread_.join();
}
