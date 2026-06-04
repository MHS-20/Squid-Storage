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

  {
    shared_lock<shared_mutex> lock(stateMutex_);
    for (auto &entry : dataNodeEndpointMap_)
      if (entry.second) entry.second->stop();
    for (auto &entry : clientEndpointMap_)
      if (entry.second) entry.second->stop();
  }

  if (acceptThread_.joinable())   acceptThread_.join();
  if (heartbeatThread_.joinable()) heartbeatThread_.join();
  if (lockExpiryThread_.joinable()) lockExpiryThread_.join();
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
      if (accepted)
        handleAccept(std::move(*accepted));
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

  if (heartbeatThread_.joinable())   heartbeatThread_.join();
  if (lockExpiryThread_.joinable()) lockExpiryThread_.join();
}

// ── Connection handling ───────────────────────────────────────────────────────

void Server::handleAccept(AcceptedConnection accepted) {
  auto channel = accepted.channel;
  if (!channel)
    return;

  SquidProtocol proto(fileManager_, channel, "[SERVER]", "SERVER");
  Message mex = proto.identify();
  string peerProcessName = mex.getString(FieldID::PROCESS_NAME);
  string peerNodeType    = mex.getString(FieldID::NODE_TYPE);
  cout << "[SERVER]: Identity received from peer: " << peerProcessName << "\n";

  if (peerNodeType == "DATANODE") {
    auto datanodeSession = make_shared<ConnectionSession>(
        fileManager_, channel, "DATANODE", peerProcessName);
    datanodeSession->start(false);

    {
      unique_lock<shared_mutex> lock(stateMutex_);
      dataNodeEndpointMap_[peerProcessName] = datanodeSession;
    }

    Message files = datanodeSession->listFiles();
    if (files.isResponse()) {
      auto fileVersionMap = files.getFileVersionMap();
      replicationManager_.registerDataNodeFiles(
          peerProcessName, datanodeSession, fileVersionMap);
      lockManager_.buildFileLockMap();
    }
    return;
  }

  if (peerNodeType != "CLIENT") {
    cout << "[SERVER]: Unknown node type\n";
    return;
  }

  proto.response(true);
  cout << "[SERVER]: Ack sent to client\n";

  auto clientSession = make_shared<ConnectionSession>(
      fileManager_, channel, "CLIENT", peerProcessName,
      [this](ConnectionSession &session, const Message &message) {
        handleClientRequest(session, message);
      });

  {
    unique_lock<shared_mutex> lock(stateMutex_);
    clientEndpointMap_[peerProcessName] = clientSession;
  }

  clientSession->start(true);

  auto fileVersionMap = replicationManager_.getFileVersionMap();
  for (auto &[filePath, version] : fileVersionMap) {
    vector<uint8_t> fileData;
    if (replicationManager_.getFileFromDataNode(filePath, fileData))
      clientSession->pushCreateFile(filePath, version, fileData);
  }
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
    bool ok = requestPool_
                  .submit([this, filePath, fileVersion,
                           origin = clientSession.getProcessName(), fileData]() {
                    return replicationManager_.propagateCreateFile(
                        filePath, fileVersion, origin, fileData, lockManager_);
                  })
                  .get();
    clientSession.response(ok);
    break;
  }
  case Opcode::READ_FILE: {
    auto result =
        requestPool_
            .submit([this, filePath]() {
              vector<uint8_t> fileData;
              bool ok = replicationManager_.getFileFromDataNode(filePath, fileData);
              return make_pair(ok, fileData);
            })
            .get();

    if (result.first) {
      clientSession.response(true);
      clientSession.response(clientSession.sendFileData(result.second));
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
    bool ok =
        requestPool_
            .submit([this, filePath, fileVersion,
                     origin = clientSession.getProcessName(), fileData]() {
              return replicationManager_.propagateUpdateFile(
                  filePath, fileVersion, origin, fileData);
            })
            .get();
    clientSession.response(ok);
    break;
  }
  case Opcode::DELETE_FILE: {
    bool ok = requestPool_
                  .submit([this, filePath,
                           origin = clientSession.getProcessName()]() {
                    replicationManager_.propagateDeleteFile(filePath, origin,
                                                           lockManager_);
                    return true;
                  })
                  .get();
    clientSession.response(ok);
    break;
  }
  case Opcode::SYNC_STATUS:
    clientSession.response(
        requestPool_
            .submit([this]() { return replicationManager_.getFileVersionMap(); })
            .get());
    break;
  case Opcode::ACQUIRE_LOCK:
    clientSession.response(
        requestPool_
            .submit([this, filePath]() { return lockManager_.acquireLock(filePath); })
            .get());
    break;
  case Opcode::RELEASE_LOCK:
    clientSession.response(
        requestPool_
            .submit([this, filePath]() { return lockManager_.releaseLock(filePath); })
            .get());
    break;
  case Opcode::HEARTBEAT:
    clientSession.response(true);
    break;
  case Opcode::CLOSE:
    clientSession.response(true);
    clientSession.closeConn();
    clientSession.setIsAlive(false);
    break;
  default:
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
