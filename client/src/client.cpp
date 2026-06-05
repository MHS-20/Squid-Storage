#include "client.hpp"

#include <iostream>
#include <stdexcept>

#include "syncplanner.hpp"

Client::Client(const std::string &serverIp, int serverPort,
               const std::string &processName)
    : Peer(serverIp, serverPort, "CLIENT", processName) {}

Client::~Client() { disconnect(); }

void Client::setPushHandler(PushHandler handler) {
  pushHandler_ = std::move(handler);
}

void Client::run() {
  connectToServer();   // uses Peer::connectToServer() — handshake is uniform
  while (isAlive())
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

ConnectionSession::RequestHandler Client::makeRequestHandler() {
  return [this](ConnectionSession &s, const Message &m) { handlePush(s, m); };
}

// handlePush is invoked by the session read loop for every incoming frame.
// PUSH_* opcodes are unambiguous server→client notifications that carry no
// response obligation. RESPONSE frames should never arrive here because the
// client's request/response operations complete inside call() on the same
// worker thread before the read loop can pick up the next frame.
void Client::handlePush(ConnectionSession &session, const Message &message) {
  switch (message.opcode) {

  // ── Server-initiated file pushes (PUSH_* opcodes) ────────────────────────
  case Opcode::PUSH_CREATE_FILE:
  case Opcode::PUSH_UPDATE_FILE: {
    std::vector<uint8_t> data;
    // receiveFileData() is safe to call directly here: we are already on the
    // session worker thread (call() takes the direct path), and the server
    // guarantees it sends file bytes in the same post() task immediately after
    // the push header, so the data is already in the TCP buffer.
    session.call([&](SquidProtocol &proto) {
      proto.receiveFileData(data);
      return 0;
    });
    const std::string path = message.getString(FieldID::FILE_PATH);
    const int version =
        static_cast<int>(message.getUint32(FieldID::FILE_VERSION, 0));
    if (!path.empty())
      fileManager_.updateFile(path, std::string(data.begin(), data.end()),
                              version);
    if (pushHandler_)
      pushHandler_(message, data);
    break;
  }

  case Opcode::PUSH_DELETE_FILE: {
    const std::string path = message.getString(FieldID::FILE_PATH);
    if (!path.empty())
      fileManager_.deleteFileAndVersion(path);
    if (pushHandler_)
      pushHandler_(message, {});
    break;
  }

  // ── Server-initiated control frames ──────────────────────────────────────
  case Opcode::RELEASE_LOCK:
    // Server notifying the client that its lock has expired.
    if (pushHandler_)
      pushHandler_(message, {});
    break;

  case Opcode::HEARTBEAT:
    // Server-initiated heartbeat: respond without going through the request
    // queue to avoid blocking the read loop.
    session.post([](SquidProtocol &proto) { proto.response(true); });
    break;

  case Opcode::CLOSE:
    session.setIsAlive(false);
    break;

  case Opcode::RESPONSE:
    // A RESPONSE arriving in the read loop means it was not consumed by a
    // pending call() — this indicates a protocol sequencing error.
    std::cerr << "[Client]: Stray RESPONSE frame in read loop — "
                 "possible push/request interleave bug: "
              << message.toString() << "\n";
    break;

  default:
    std::cerr << "[Client]: Unexpected opcode in push handler: "
              << message.toString() << "\n";
    break;
  }
}

// ── Client-initiated file operations ─────────────────────────────────────────
// All of these go through call(), which serialises them on the session worker
// thread. The read loop is suspended while call() is executing, so there is no
// risk of a PUSH frame being consumed by waitForAck() inside the operation.

Message Client::createFile(const std::string &filePath,
                           const std::vector<uint8_t> &data, int version) {
  if (!isAlive()) return {};
  return session_->call([filePath, version, data](SquidProtocol &proto) {
    return proto.createFile(filePath, version, data);
  });
}

Message Client::readFile(const std::string &filePath,
                         std::vector<uint8_t> &dataOut) {
  if (!isAlive()) return {};
  return session_->call([filePath, &dataOut](SquidProtocol &proto) {
    return proto.readFile(filePath, dataOut);
  });
}

Message Client::updateFile(const std::string &filePath,
                           const std::vector<uint8_t> &data, int version) {
  if (!isAlive()) return {};
  return session_->call([filePath, version, data](SquidProtocol &proto) {
    return proto.updateFile(filePath, version, data);
  });
}

Message Client::deleteFile(const std::string &filePath) {
  if (!isAlive()) return {};
  return session_->call([filePath](SquidProtocol &proto) {
    return proto.deleteFile(filePath);
  });
}

Message Client::acquireLock(const std::string &filePath) {
  if (!isAlive()) return {};
  return session_->call([filePath](SquidProtocol &proto) {
    return proto.acquireLock(filePath);
  });
}

Message Client::releaseLock(const std::string &filePath) {
  if (!isAlive()) return {};
  return session_->call([filePath](SquidProtocol &proto) {
    return proto.releaseLock(filePath);
  });
}

Message Client::syncStatus() {
  if (!isAlive()) return {};

  Message response = session_->call([](SquidProtocol &proto) {
    return proto.syncStatus();
  });

  if (!response.isResponse())
    return response;

  auto remoteMap = response.getFileVersionMap();
  auto localMap =
      fileManager_.getFileVersionMap(FileManager::storageRoot().string());
  auto ops = planSync(localMap, remoteMap);

  for (const auto &op : ops) {
    switch (op.action) {
    case SyncAction::UPLOAD:
      session_->call([op](SquidProtocol &proto) {
        return proto.updateFile(op.filePath, op.version);
      });
      break;
    case SyncAction::CREATE_REMOTE:
      session_->call([op](SquidProtocol &proto) {
        return proto.createFile(op.filePath, op.version);
      });
      break;
    case SyncAction::DOWNLOAD:
      session_->call([op](SquidProtocol &proto) {
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
