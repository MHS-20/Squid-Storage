#include "peer.hpp"

Peer::Peer() {}

Peer::Peer(std::string nodeType, std::string processName)
    : Peer(SERVER_IP, SERVER_PORT, std::move(nodeType),
           std::move(processName)) {}

Peer::Peer(int port, std::string nodeType, std::string processName)
    : Peer(SERVER_IP, port, std::move(nodeType), std::move(processName)) {}

Peer::Peer(std::string server_ip, int port, std::string nodeType,
           std::string processName)
    : port(port), server_ip(server_ip), nodeType_(std::move(nodeType)),
      processName_(std::move(processName)) {}

Peer::~Peer() { disconnect(); }

std::shared_ptr<INetworkChannel> Peer::tryConnectChannel(const std::string &ip,
                                                         int p) {
  try {
    auto ch = std::make_shared<TCPConnectorChannel>(ip.c_str(), p, 2, 1);
    if (ch->isOpen())
      return ch;
  } catch (...) {
  }
  return nullptr;
}

bool Peer::performHandshake(std::shared_ptr<INetworkChannel> channel) {
  SquidProtocol proto(fileManager_, channel, nodeType_, processName_);

  Message identify = proto.receiveAndParse();
  if (!proto.isAlive() || identify.opcode != Opcode::IDENTIFY)
    return false;

  proto.response(nodeType_, processName_);
  if (!proto.isAlive())
    return false;

  Message ack = proto.receiveAndParse();
  if (!proto.isAlive() || !ack.isAck())
    return false;

  uint32_t serverEpoch = ack.getUint32(FieldID::EPOCH, 0);
  if (serverEpoch < lastSeenEpoch_) {
    proto.sendNackStaleEpoch(lastSeenEpoch_);
    return false;
  }
  if (serverEpoch > lastSeenEpoch_)
    lastSeenEpoch_ = serverEpoch;

  return true;
}

void Peer::connectToServer() {
  auto channel = std::make_shared<TCPConnectorChannel>(server_ip.c_str(), port,
                                                       timeoutSeconds, 3);
  std::cout << nodeType_ << ": Connected to server\n";

  if (!performHandshake(channel))
    throw std::runtime_error(nodeType_ + ": handshake failed");

  std::cout << nodeType_ << ": Handshake complete\n";

  session_ = std::make_shared<ConnectionSession>(
      fileManager_, channel, nodeType_, processName_, makeRequestHandler());
  session_->start(true);
  onConnected();
}

void Peer::connectWithFailover(const ClusterConfig &config) {
  clusterConfig_ = config;

  int attempts = std::max(1, config.reconnect_attempts);
  int delayMs = config.reconnect_delay_ms;

  for (int attempt = 0; attempt < attempts; ++attempt) {
    for (const auto &entry : config.servers) {
      auto channel = tryConnectChannel(entry.ip, entry.port);
      if (!channel)
        continue;

      if (!performHandshake(channel)) {
        channel->close();
        continue;
      }

      std::cout << nodeType_ << ": Connected to '" << entry.name << "' ("
                << entry.ip << ":" << entry.port << ")\n";

      server_ip = entry.ip;
      port = entry.port;

      session_ = std::make_shared<ConnectionSession>(
          fileManager_, channel, nodeType_, processName_, makeRequestHandler());
      session_->start(true);
      onConnected();
      return;
    }

    if (attempt + 1 < attempts)
      std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
  }

  throw std::runtime_error(nodeType_ +
                           ": could not connect to any server in cluster");
}

void Peer::reconnect() {
  disconnect();
  if (clusterConfig_.valid())
    connectWithFailover(clusterConfig_);
  else
    connectToServer();
}

void Peer::disconnect() {
  if (!session_)
    return;
  if (session_->isAlive())
    session_->closeConn();
  session_->stop();
  session_.reset();
}

bool Peer::isAlive() const { return session_ && session_->isAlive(); }
