#include "peer.hpp"

Peer::Peer() {}

Peer::Peer(std::string nodeType, std::string processName)
    : Peer(SERVER_IP, SERVER_PORT, std::move(nodeType), std::move(processName)) {}

Peer::Peer(int port, std::string nodeType, std::string processName)
    : Peer(SERVER_IP, port, std::move(nodeType), std::move(processName)) {}

Peer::Peer(std::string server_ip, int port, std::string nodeType,
           std::string processName)
    : port(port), server_ip(server_ip),
      nodeType_(std::move(nodeType)), processName_(std::move(processName)) {}

Peer::~Peer() { disconnect(); }

void Peer::connectToServer() {
  auto channel = std::make_shared<TCPConnectorChannel>(
      server_ip.c_str(), port, timeoutSeconds, 3);
  std::cout << nodeType_ << ": Connected to server\n";

  SquidProtocol proto(fileManager_, channel, nodeType_, processName_);
  Message identify = proto.receiveAndParse();
  if (identify.opcode != Opcode::IDENTIFY)
    throw std::runtime_error(nodeType_ + ": handshake failed");

  proto.response(nodeType_, processName_);

  session_ = std::make_shared<ConnectionSession>(
      fileManager_, channel, nodeType_, processName_, makeRequestHandler());
  session_->start(true);
  onConnected();
}

void Peer::reconnect() {
  disconnect();
  connectToServer();
}

void Peer::disconnect() {
  if (!session_) return;
  if (session_->isAlive())
    session_->call([](SquidProtocol &p) { return p.closeConn(); });
  session_->stop();
  session_.reset();
}

bool Peer::isAlive() const { return session_ && session_->isAlive(); }