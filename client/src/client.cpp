#include "client.hpp"

#include <iostream>
#include <stdexcept>

#include "syncplanner.hpp"

Client::Client(const std::string &serverIp, int serverPort,
               const std::string &processName)
    : Peer(serverIp.c_str(), serverPort, "CLIENT", processName) {}

Client::~Client() { disconnect(); }

void Client::setPushHandler(PushHandler handler) {
  pushHandler_ = std::move(handler);
}

void Client::doHandshake(SquidProtocol &proto) {
  Message identify = proto.receiveAndParse();
  if (identify.opcode != Opcode::IDENTIFY)
    throw std::runtime_error("Client: expected IDENTIFY, got " +
                             identify.toString());

  proto.response(std::string("CLIENT"), processName_);

  Message ack = proto.receiveAndParse();
  if (!ack.isAck())
    throw std::runtime_error("Client: handshake ACK not received");

  std::cout << "[Client]: Handshake complete with server\n";
}

void Client::run() {
  connectToServer();
  while (isAlive())
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

ConnectionSession::RequestHandler Client::makeRequestHandler() {
  return [this](ConnectionSession &s, const Message &m) { handlePush(s, m); };
}

void Client::connectToServer() {
  auto channel = std::make_shared<TCPConnectorChannel>(
      server_ip, port, timeoutSeconds, 2);
  std::cout << "[Client]: Connected to server\n";

  SquidProtocol proto(fileManager_, channel, nodeType_, processName_);
  Message identify = proto.receiveAndParse();
  if (identify.opcode != Opcode::IDENTIFY)
    throw std::runtime_error("Client: expected IDENTIFY");

  proto.response(nodeType_, processName_);

  Message ack = proto.receiveAndParse();
  if (!ack.isAck())
    throw std::runtime_error("Client: handshake ACK not received");

  std::cout << "[Client]: Handshake complete\n";

  session_ = std::make_shared<ConnectionSession>(
      fileManager_, channel, nodeType_, processName_, makeRequestHandler());
  session_->start(true);
}

void Client::handlePush(ConnectionSession &session, const Message &message) {
  if (message.isResponse())
    return;

  switch (message.opcode) {
  case Opcode::CREATE_FILE:
  case Opcode::UPDATE_FILE: {
    std::vector<uint8_t> data;
    session.call([&](SquidProtocol &proto) {
      proto.receiveFileData(data);
      return 0;
    });
    const std::string path = message.getString(FieldID::FILE_PATH);
    const int version = static_cast<int>(message.getUint32(FieldID::FILE_VERSION, 0));
    if (!path.empty())
      fileManager_.updateFile(path, std::string(data.begin(), data.end()), version);
    if (pushHandler_)
      pushHandler_(message, data);
    break;
  }
  case Opcode::DELETE_FILE:
  case Opcode::RELEASE_LOCK:
    if (pushHandler_)
      pushHandler_(message, {});
    break;
  case Opcode::CLOSE:
    session.setIsAlive(false);
    break;
  case Opcode::HEARTBEAT:
    session.post([](SquidProtocol &proto) { proto.response(true); });
    break;
  default:
    std::cerr << "[Client]: Unexpected push opcode: " << message.toString() << "\n";
    break;
  }
}

Message Client::createFile(const std::string &filePath,
                           const std::vector<uint8_t> &data, int version) {
  if (!isAlive()) return {};
  return session_->call([&](SquidProtocol &proto) {
    return proto.createFile(filePath, version, data);
  });
}

Message Client::readFile(const std::string &filePath,
                         std::vector<uint8_t> &dataOut) {
  if (!isAlive()) return {};
  return session_->call([&](SquidProtocol &proto) {
    return proto.readFile(filePath, dataOut);
  });
}

Message Client::updateFile(const std::string &filePath,
                           const std::vector<uint8_t> &data, int version) {
  if (!isAlive()) return {};
  return session_->call([&](SquidProtocol &proto) {
    return proto.updateFile(filePath, version, data);
  });
}

Message Client::deleteFile(const std::string &filePath) {
  if (!isAlive()) return {};
  return session_->call([&](SquidProtocol &proto) {
    return proto.deleteFile(filePath);
  });
}

Message Client::acquireLock(const std::string &filePath) {
  if (!isAlive()) return {};
  return session_->call([&](SquidProtocol &proto) {
    return proto.acquireLock(filePath);
  });
}

Message Client::releaseLock(const std::string &filePath) {
  if (!isAlive()) return {};
  return session_->call([&](SquidProtocol &proto) {
    return proto.releaseLock(filePath);
  });
}

Message Client::syncStatus() {
  if (!isAlive()) return {};

  Message response = session_->call([](SquidProtocol &proto) {
    return proto.syncStatus();
  });

  if (!response.isResponse() || response.isAck())
    return response;

  auto remoteMap = response.getFileVersionMap();
  auto localMap = fileManager_.getFileVersionMap(FileManager::storageRoot().string());
  auto ops = planSync(localMap, remoteMap);

  for (const auto &op : ops) {
    switch (op.action) {
    case SyncAction::UPLOAD:
      session_->call([&](SquidProtocol &proto) {
        return proto.updateFile(op.filePath, op.version);
      });
      break;
    case SyncAction::CREATE_REMOTE:
      session_->call([&](SquidProtocol &proto) {
        return proto.createFile(op.filePath, op.version);
      });
      break;
    case SyncAction::DOWNLOAD:
      session_->call([&](SquidProtocol &proto) {
        return proto.readFile(op.filePath);
      });
      fileManager_.setFileVersion(op.filePath, op.version);
      break;
    }
  }

  return response;
}

Message Client::heartbeat() {
  if (!isAlive()) return {};
  return session_->call([](SquidProtocol &proto) { return proto.heartbeat(); });
}