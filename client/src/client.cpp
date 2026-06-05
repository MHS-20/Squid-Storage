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
  connectToServer();
  // syncStatus() reconciles any files created or updated locally while offline.
  // We must NOT call it from onConnected() because Peer::connectToServer()
  // returns before the server's initial PUSH_CREATE_FILE burst has been read
  // from the wire: the session worker hasn't run yet, so the TCP buffer still
  // holds unread push frames. Submitting a SYNC_STATUS task to the queue would
  // run it BEFORE those frames are consumed, desynchronising the stream.
  //
  // Calling it here (from the main thread, after connectToServer() returns) is
  // safe: session_->call() dispatches to the worker thread where it will be
  // executed *after* the worker's read loop has processed all pending frames.
  syncStatus();
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

// ── helpers ───────────────────────────────────────────────────────────────────

// Extract the FILE_VERSION field from an ACK. Returns -1 when the field is
// absent (e.g. the server is an older build that doesn't send it yet).
static int versionFromAck(const Message &ack) {
  if (!ack.isAck()) return -1;
  uint32_t v = ack.getUint32(FieldID::FILE_VERSION, UINT32_MAX);
  return (v == UINT32_MAX) ? -1 : static_cast<int>(v);
}

// ── Client-initiated file operations ─────────────────────────────────────────
// All of these go through call(), which serialises them on the session worker
// thread. The read loop is suspended while call() is executing, so there is no
// risk of a PUSH frame being consumed by waitForAck() inside the operation.
//
// After each successful operation the server's authoritative version is read
// from the ACK's FILE_VERSION field and persisted in the local map.  The
// client never invents version numbers; it only echoes back the last version it
// has seen so the server can compute the next one.

Message Client::createFile(const std::string &filePath,
                           const std::vector<uint8_t> &data, int version) {
  if (!isAlive()) return {};
  Message ack = session_->call([&filePath, version, &data](SquidProtocol &proto) {
    return proto.createFile(filePath, version, data);
  });
  int newVersion = versionFromAck(ack);
  if (newVersion >= 0)
    fileManager_.setFileVersion(filePath, newVersion);
  return ack;
}

Message Client::readFile(const std::string &filePath,
                         std::vector<uint8_t> &dataOut) {
  if (!isAlive()) return {};
  Message ack = session_->call([&filePath, &dataOut](SquidProtocol &proto) {
    return proto.readFile(filePath, dataOut);
  });
  int newVersion = versionFromAck(ack);
  if (newVersion >= 0)
    fileManager_.setFileVersion(filePath, newVersion);
  return ack;
}

Message Client::updateFile(const std::string &filePath,
                           const std::vector<uint8_t> &data, int version) {
  if (!isAlive()) return {};
  Message ack = session_->call([&filePath, version, &data](SquidProtocol &proto) {
    return proto.updateFile(filePath, version, data);
  });
  int newVersion = versionFromAck(ack);
  if (newVersion >= 0)
    fileManager_.setFileVersion(filePath, newVersion);
  return ack;
}

Message Client::deleteFile(const std::string &filePath) {
  if (!isAlive()) return {};
  Message ack = session_->call([&filePath](SquidProtocol &proto) {
    return proto.deleteFile(filePath);
  });
  if (ack.isAck())
    fileManager_.deleteFileAndVersion(filePath);
  return ack;
}

Message Client::acquireLock(const std::string &filePath) {
  if (!isAlive()) return {};
  return session_->call([&filePath](SquidProtocol &proto) {
    return proto.acquireLock(filePath);
  });
}

Message Client::releaseLock(const std::string &filePath) {
  if (!isAlive()) return {};
  return session_->call([&filePath](SquidProtocol &proto) {
    return proto.releaseLock(filePath);
  });
}

Message Client::syncStatus() {
  if (!isAlive()) return {};

  // Ask the server for its current version map.
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

    case SyncAction::UPLOAD: {
      // File exists on both sides; our version is newer.  Send it and record
      // the authoritative version that comes back in the ACK.
      std::string content = fileManager_.readFile(op.filePath);
      std::vector<uint8_t> data(content.begin(), content.end());
      Message ack = session_->call([&op, &data](SquidProtocol &proto) {
        return proto.updateFile(op.filePath, op.version, data);
      });
      int newVersion = versionFromAck(ack);
      if (newVersion >= 0)
        fileManager_.setFileVersion(op.filePath, newVersion);
      break;
    }

    case SyncAction::CREATE_REMOTE: {
      // File exists locally but the server has never seen it.  Create it
      // remotely using the local content and let the server assign the
      // authoritative version.
      std::string content = fileManager_.readFile(op.filePath);
      std::vector<uint8_t> data(content.begin(), content.end());
      Message ack = session_->call([&op, &data](SquidProtocol &proto) {
        return proto.createFile(op.filePath, op.version, data);
      });
      int newVersion = versionFromAck(ack);
      if (newVersion >= 0)
        fileManager_.setFileVersion(op.filePath, newVersion);
      break;
    }

    case SyncAction::DOWNLOAD: {
      // Server has a newer (or new-to-us) file.  Download it and record the
      // version from the ACK, not the snapshot version, so a concurrent server
      // update doesn't leave us with a stale entry.
      std::vector<uint8_t> data;
      Message ack = session_->call([&op, &data](SquidProtocol &proto) {
        return proto.readFile(op.filePath, data);
      });
      int newVersion = versionFromAck(ack);
      if (newVersion >= 0)
        fileManager_.setFileVersion(op.filePath, newVersion);
      break;
    }
    }
  }

  return response;
}

Message Client::heartbeat() {
  if (!isAlive()) return {};
  return session_->call([](SquidProtocol &proto) { return proto.heartbeat(); });
}
