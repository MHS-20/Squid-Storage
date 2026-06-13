#include "client.hpp"
#include "syncplanner.hpp"
#include <iostream>

Client::Client(const std::string &serverIp, int serverPort,
               const std::string &processName)
    : Peer(serverIp, serverPort, "CLIENT", processName) {}

Client::Client(const std::string &processName)
    : Peer("CLIENT", processName) {}

Client::~Client() { disconnect(); }

void Client::setPushHandler(PushHandler handler) {
  pushHandler_ = std::move(handler);
}

void Client::run() {
  connectToServer();
  syncStatus();
  while (isAlive())
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// ── Reconnect helper

bool Client::ensureConnected() {
  if (isAlive())
    return true;
  try {
    reconnect();
  } catch (const std::exception &e) {
    std::cerr << processName_ << ": reconnect failed: " << e.what() << "\n";
    return false;
  }
  if (!isAlive())
    return false;
  // Refresh version map from the new server.
  syncStatus();
  return true;
}

// ── Version map

int Client::getFileVersion(const std::string &path) const {
  std::lock_guard<std::mutex> lk(versionMutex_);
  auto it = versions_.find(path);
  return it != versions_.end() ? it->second : -1;
}

void Client::setFileVersion(const std::string &path, int version) {
  std::lock_guard<std::mutex> lk(versionMutex_);
  versions_[path] = version;
}

void Client::deleteFileVersion(const std::string &path) {
  std::lock_guard<std::mutex> lk(versionMutex_);
  versions_.erase(path);
}

std::map<std::string, int> Client::getVersionMap() const {
  std::lock_guard<std::mutex> lk(versionMutex_);
  return versions_;
}

void Client::setVersionFromAck(const std::string &path, const Message &ack) {
  if (!ack.isAck())
    return;
  uint32_t v = ack.getUint32(FieldID::FILE_VERSION, UINT32_MAX);
  if (v != UINT32_MAX)
    setFileVersion(path, static_cast<int>(v));
}

// ── Push handler

ConnectionSession::RequestHandler Client::makeRequestHandler() {
  return [this](ConnectionSession &s, const Message &m) { handlePush(s, m); };
}

void Client::handlePush(ConnectionSession &session, const Message &message) {
  switch (message.opcode) {

  case Opcode::PUSH_CREATE_FILE:
  case Opcode::PUSH_UPDATE_FILE: {
    std::vector<uint8_t> data;
    session.call([&](SquidProtocol &proto) {
      proto.receiveFileData(data);
      return 0;
    });
    const std::string path = message.getString(FieldID::FILE_PATH);
    const int version =
        static_cast<int>(message.getUint32(FieldID::FILE_VERSION, 0));
    if (!path.empty())
      setFileVersion(path, version); // version map only — no disk write
    if (pushHandler_)
      pushHandler_(message, data);
    break;
  }

  case Opcode::PUSH_DELETE_FILE: {
    const std::string path = message.getString(FieldID::FILE_PATH);
    if (!path.empty())
      deleteFileVersion(path);
    if (pushHandler_)
      pushHandler_(message, {});
    break;
  }

  case Opcode::RELEASE_LOCK:
    if (pushHandler_)
      pushHandler_(message, {});
    break;

  case Opcode::HEARTBEAT:
    session.post([](SquidProtocol &proto) { proto.response(true); });
    break;

  case Opcode::CLOSE:
    session.setIsAlive(false);
    break;

  case Opcode::RESPONSE:
    std::cerr << "[Client]: Stray RESPONSE frame: " << message.toString()
              << "\n";
    break;

  case Opcode::NACK_STALE_EPOCH:
    std::cerr << "[Client]: NACK_STALE_EPOCH — disconnecting\n";
    session.setIsAlive(false);
    if (pushHandler_)
      pushHandler_(message, {});
    break;

  default:
    std::cerr << "[Client]: Unexpected opcode: " << message.toString() << "\n";
    break;
  }
}

// ── File operations

Message Client::createFile(const std::string &filePath,
                           const std::vector<uint8_t> &data, int version) {
  if (!ensureConnected())
    return {};
  Message ack = session_->call([&](SquidProtocol &proto) {
    return proto.createFile(filePath, version, data);
  });
  setVersionFromAck(filePath, ack);
  return ack;
}

Message Client::readFile(const std::string &filePath,
                         std::vector<uint8_t> &dataOut) {
  if (!ensureConnected())
    return {};
  Message ack = session_->call(
      [&](SquidProtocol &proto) { return proto.readFile(filePath, dataOut); });
  setVersionFromAck(filePath, ack);
  return ack;
}

Message Client::updateFile(const std::string &filePath,
                           const std::vector<uint8_t> &data, int version) {
  if (!ensureConnected())
    return {};
  Message ack = session_->call([&](SquidProtocol &proto) {
    return proto.updateFile(filePath, version, data);
  });
  setVersionFromAck(filePath, ack);
  return ack;
}

Message Client::deleteFile(const std::string &filePath) {
  if (!ensureConnected())
    return {};
  Message ack = session_->call(
      [&](SquidProtocol &proto) { return proto.deleteFile(filePath); });
  if (ack.isAck())
    deleteFileVersion(filePath);
  return ack;
}

Message Client::acquireLock(const std::string &filePath) {
  if (!ensureConnected())
    return {};
  return session_->call(
      [&](SquidProtocol &proto) { return proto.acquireLock(filePath); });
}

Message Client::releaseLock(const std::string &filePath) {
  if (!ensureConnected())
    return {};
  return session_->call(
      [&](SquidProtocol &proto) { return proto.releaseLock(filePath); });
}

Message Client::syncStatus() {
  if (!isAlive())
    return {};

  Message response =
      session_->call([](SquidProtocol &proto) { return proto.syncStatus(); });
  if (!response.isResponse())
    return response;

  auto remoteMap = response.getFileVersionMap();
  {
    std::lock_guard<std::mutex> lk(versionMutex_);
    for (const auto &[path, version] : remoteMap)
      versions_[path] = version;
  }

  return response;
}

Message Client::heartbeat() {
  if (!isAlive())
    return {};
  return session_->call([](SquidProtocol &proto) { return proto.heartbeat(); });
}
