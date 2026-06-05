#include "server.hpp"
#include "networking/TCPConnectorChannel.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>

using namespace std;

// ── Constructors ─────────────────────────────────────────────────────────────

Server::Server() : Server(DEFAULT_PORT, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port) : Server(port, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port, int replicationFactor)
    : Server(port, replicationFactor, DEFAULT_TIMEOUT) {}

Server::Server(int port, int replicationFactor, int timeoutSeconds)
    : port_(port),
      replicationFactor_(replicationFactor),
      listener_(std::make_unique<TCPListenerChannel>(port, 3)),
      lockManager_(stateMutex_, dataNodeEndpointMap_, clientEndpointMap_,
                   fileManager_),
      replicationManager_(replicationFactor_, stateMutex_, dataNodeEndpointMap_,
                          clientEndpointMap_, fileManager_, requestPool_),
      heartbeatManager_(stateMutex_, dataNodeEndpointMap_, requestPool_,
                        replicationManager_) {
  (void)timeoutSeconds;
}

Server::~Server() {
  running_ = false;
  if (listener_)
    listener_->close();

  // Join background threads before touching the session maps so we know no
  // thread is still reading/writing them. Use unique_lock for stop() calls.
  if (acceptThread_.joinable())    acceptThread_.join();
  if (heartbeatThread_.joinable()) heartbeatThread_.join();
  if (lockExpiryThread_.joinable()) lockExpiryThread_.join();

  // Now it is safe to stop all sessions without holding any lock.
  for (auto &entry : dataNodeEndpointMap_)
    if (entry.second) entry.second->stop();
  for (auto &entry : clientEndpointMap_)
    if (entry.second) entry.second->stop();
}

// ── Run loop ──────────────────────────────────────────────────────────────────

void Server::run() {
  cout << "[SERVER]: Starting on port " << port_ << "...\n";
  running_ = true;

  acceptThread_ = thread([this]() {
    while (running_) {
      if (!listener_) {
        cerr << "[SERVER]: Listener not available\n";
        running_ = false;
        return;
      }
      auto accepted = listener_->waitForConnection(1);
      if (!accepted)
        continue;

      // Offload the entire accept/handshake/init to the thread pool so the
      // accept loop is never blocked by slow handshakes or initial file syncs.
      AcceptedConnection conn = std::move(*accepted);
      requestPool_.submit([this, conn = std::move(conn)]() mutable {
        handleAccept(std::move(conn));
      });
    }
  });

  heartbeatThread_ = thread([this]() {
    while (running_) {
      heartbeatManager_.sendHeartbeats();
      this_thread::sleep_for(chrono::seconds(2));
    }
  });

  lockExpiryThread_ = thread([this]() {
    while (running_) {
      lockManager_.checkFileLockExpiration();
      this_thread::sleep_for(chrono::seconds(1));
    }
  });

  if (acceptThread_.joinable())
    acceptThread_.join();

  running_ = false;

  if (heartbeatThread_.joinable())    heartbeatThread_.join();
  if (lockExpiryThread_.joinable()) lockExpiryThread_.join();
}

// ── Connection handling ───────────────────────────────────────────────────────

void Server::handleAccept(AcceptedConnection accepted) {
  auto channel = accepted.channel;
  if (!channel)
    return;

  // Perform the handshake on the calling thread (a requestPool_ worker).
  // This keeps the accept loop free for new connections.
  SquidProtocol proto(fileManager_, channel, "[SERVER]", "SERVER");

  // Step 1: send IDENTIFY so the peer knows to send us its identity.
  Message mex = proto.identify();
  if (!proto.isAlive()) {
    cerr << "[SERVER]: Handshake failed — peer disconnected during IDENTIFY\n";
    return;
  }

  string peerProcessName = mex.getString(FieldID::PROCESS_NAME);
  string peerNodeType    = mex.getString(FieldID::NODE_TYPE);

  if (peerNodeType != "DATANODE" && peerNodeType != "CLIENT") {
    cout << "[SERVER]: Unknown node type '" << peerNodeType << "' — dropping\n";
    return;
  }

  // Step 2: ACK the identity for both DATANODEs and CLIENTs uniformly.
  proto.response(true);
  if (!proto.isAlive()) {
    cerr << "[SERVER]: Handshake failed — could not send ACK to "
         << peerProcessName << "\n";
    return;
  }

  cout << "[SERVER]: Handshake complete with " << peerNodeType
       << " '" << peerProcessName << "'\n";

  // ── DATANODE path ────────────────────────────────────────────────────────
  if (peerNodeType == "DATANODE") {
    // Datanodes are purely passive: the server sends requests, the datanode
    // responds. No read loop needed on the server side (readLoop=false).
    auto datanodeSession = make_shared<ConnectionSession>(
        fileManager_, channel, "DATANODE", peerProcessName);
    datanodeSession->start(false);

    {
      unique_lock<shared_mutex> lock(stateMutex_);
      dataNodeEndpointMap_[peerProcessName] = datanodeSession;
    }

    // Query the new datanode's file list and register it with the managers.
    Message files = datanodeSession->syncStatus();
    if (files.isResponse()) {
      auto fileVersionMap = files.getFileVersionMap();
      replicationManager_.registerDataNodeFiles(
          peerProcessName, datanodeSession, fileVersionMap);
      lockManager_.buildFileLockMap();
    }
    return;
  }

  // ── CLIENT path ───────────────────────────────────────────────────────────
  // Build the session first but do NOT start it yet. We push all existing
  // files synchronously before starting the read loop so there is no window
  // where the client can send a request while we are mid-push.
  auto clientSession = make_shared<ConnectionSession>(
      fileManager_, channel, "CLIENT", peerProcessName,
      [this](ConnectionSession &session, const Message &message) {
        handleClientRequest(session, message);
      });

  {
    unique_lock<shared_mutex> lock(stateMutex_);
    clientEndpointMap_[peerProcessName] = clientSession;
  }

  // Push all existing files before starting the read loop.
  // pushCreateFile uses post() which enqueues into the session task queue.
  // start(true) drains that queue before entering the select() loop, so all
  // pushes are sent before the first client request can be processed.
  auto fileVersionMap = replicationManager_.getFileVersionMap();
  for (auto &[filePath, version] : fileVersionMap) {
    vector<uint8_t> fileData;
    if (replicationManager_.getFileFromDataNode(filePath, fileData))
      clientSession->pushCreateFile(filePath, version, fileData);
  }

  // Now start the read loop. The worker thread will first drain the push queue
  // (all pushCreateFile posts above) and only then begin reading from the socket.
  clientSession->start(true);
}

void Server::handleClientRequest(ConnectionSession &clientSession,
                                 const Message &mex) {
  string filePath    = mex.getString(FieldID::FILE_PATH);
  int    fileVersion = static_cast<int>(mex.getUint32(FieldID::FILE_VERSION, 0));

  switch (mex.opcode) {
  case Opcode::CREATE_FILE: {
    clientSession.response(true);
    vector<uint8_t> fileData;
    if (!clientSession.receiveFileData(fileData)) {
      clientSession.response(false);
      break;
    }
    // propagateCreateFile fans out via ConnectionSession::call() on each
    // datanode's session thread — no additional pool thread needed.
    bool ok = replicationManager_.propagateCreateFile(
        filePath, fileVersion, clientSession.getProcessName(),
        fileData, lockManager_);
    clientSession.response(ok);
    break;
  }

  case Opcode::READ_FILE: {
    // getFileFromDataNode dispatches to the datanode's session worker thread
    // via ConnectionSession::call() — no pool thread needed.
    vector<uint8_t> fileData;
    bool ok = replicationManager_.getFileFromDataNode(filePath, fileData);
    if (ok) {
      clientSession.response(true);
      clientSession.response(clientSession.sendFileData(fileData));
    } else {
      clientSession.response(false);
    }
    break;
  }

  case Opcode::UPDATE_FILE: {
    clientSession.response(true);
    vector<uint8_t> fileData;
    if (!clientSession.receiveFileData(fileData)) {
      clientSession.response(false);
      break;
    }
    // propagateUpdateFile fans out via ConnectionSession::call() on each
    // datanode's session thread — no additional pool thread needed.
    bool ok = replicationManager_.propagateUpdateFile(
        filePath, fileVersion, clientSession.getProcessName(), fileData);
    clientSession.response(ok);
    break;
  }

  case Opcode::DELETE_FILE: {
    replicationManager_.propagateDeleteFile(
        filePath, clientSession.getProcessName(), lockManager_);
    clientSession.response(true);
    break;
  }

  case Opcode::SYNC_STATUS:
    // handleClientRequest is already executing on a requestPool_ worker thread.
    // Calling getFileVersionMap() directly avoids a nested submit() that would
    // block this thread waiting for another pool thread — which can deadlock
    // when the pool is fully occupied.
    clientSession.response(replicationManager_.getFileVersionMap());
    break;

  case Opcode::ACQUIRE_LOCK:
    // acquireLock is a pure in-memory operation — no pool thread needed.
    clientSession.response(
        lockManager_.acquireLock(filePath, clientSession.getProcessName()));
    break;

  case Opcode::RELEASE_LOCK:
    clientSession.response(lockManager_.releaseLock(filePath));
    break;

  case Opcode::HEARTBEAT:
    clientSession.response(true);
    break;

  case Opcode::CLOSE:
    // ACK the close and mark the session dead. Do NOT call closeConn() here —
    // that would send a second CLOSE frame and block waiting for a response
    // from a peer that has already closed its side.
    // The map erase is posted to the thread pool instead of done inline:
    // this handler runs on the session's own worker thread, and erasing the
    // shared_ptr here could drop the last reference, triggering
    // ~ConnectionSession -> stop() -> worker_.join() on the running thread.
    clientSession.response(true);
    clientSession.setIsAlive(false);
    requestPool_.submit([this, name = clientSession.getProcessName()]() {
      unique_lock<shared_mutex> lock(stateMutex_);
      clientEndpointMap_.erase(name);
    });
    break;

  default:
    cerr << "[SERVER]: Unknown opcode from client: " << mex.toString() << "\n";
    clientSession.response(false);
    break;
  }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

shared_ptr<ConnectionSession> Server::getDataNodeSession(const string &name) {
  shared_lock<shared_mutex> lock(stateMutex_);
  auto it = dataNodeEndpointMap_.find(name);
  return it == dataNodeEndpointMap_.end() ? nullptr : it->second;
}

shared_ptr<ConnectionSession> Server::getClientSession(const string &name) {
  shared_lock<shared_mutex> lock(stateMutex_);
  auto it = clientEndpointMap_.find(name);
  return it == clientEndpointMap_.end() ? nullptr : it->second;
}

void Server::printMap(map<string, long long> &m, const string &name) {
  cout << "[SERVER]: " << name << "\n";
  for (auto &pair : m)
    cout << pair.first << " => " << pair.second << "\n";
}

void Server::printMap(map<string, shared_ptr<ConnectionSession>> &m,
                      const string &name) {
  cout << "[SERVER]: " << name << "\n";
  for (auto &pair : m)
    cout << pair.first << " => "
         << (pair.second ? pair.second->toString() : "<null>") << "\n";
}
