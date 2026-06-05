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

  // Handshake using a temporary protocol object on the caller's thread.
  // The channel is shared_ptr so bytes written/read here are visible to the
  // session we build afterwards on the same channel.
  SquidProtocol proto(fileManager_, channel, nodeType_, processName_);

  // Step 1: receive IDENTIFY
  Message identify = proto.receiveAndParse();
  if (!proto.isAlive() || identify.opcode != Opcode::IDENTIFY)
    throw std::runtime_error(nodeType_ + ": handshake failed — expected IDENTIFY");

  // Step 2: send our identity
  proto.response(nodeType_, processName_);
  if (!proto.isAlive())
    throw std::runtime_error(nodeType_ + ": handshake failed — could not send identity");

  // Step 3: wait for ACK (server sends this to all peer types uniformly)
  Message ack = proto.receiveAndParse();
  if (!proto.isAlive() || !ack.isAck())
    throw std::runtime_error(nodeType_ + ": handshake failed — ACK not received");

  std::cout << nodeType_ << ": Handshake complete\n";

  // Build the long-lived session on the same channel.
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
    session_->closeConn();
  session_->stop();
  session_.reset();
}

bool Peer::isAlive() const { return session_ && session_->isAlive(); }
