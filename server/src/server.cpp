#include "server.hpp"
#include "networking/TCPConnectorChannel.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <utility>

using namespace std;

Server::Server() : Server(DEFAULT_PORT, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port) : Server(port, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port, int replicationFactor)
    : Server(port, replicationFactor, 0) {}

Server::Server(int port, int replicationFactor,
               uint32_t epoch)
    : port_(port), replicationFactor_(replicationFactor), epoch_(epoch),
      listener_(std::make_unique<TCPListenerChannel>(port, 3)),
      lockManager_(stateMutex_, dataNodeEndpointMap_, clientEndpointMap_,
                   fileManager_),
      replicationManager_(replicationFactor_, stateMutex_, dataNodeEndpointMap_,
                          clientEndpointMap_, fileManager_, requestPool_),
      heartbeatManager_(stateMutex_, dataNodeEndpointMap_, requestPool_,
                        replicationManager_) {}


Server::~Server() {
  running_ = false;
  if (listener_)
    listener_->close();

  // Clean up readiness flag.
  std::remove("/tmp/squid_ready");

  if (acceptThread_.joinable())
    acceptThread_.join();
  if (heartbeatThread_.joinable())
    heartbeatThread_.join();
  if (lockExpiryThread_.joinable())
    lockExpiryThread_.join();
  if (standbyHbThread_.joinable())
    standbyHbThread_.join();

  for (auto &entry : dataNodeEndpointMap_)
    if (entry.second)
      entry.second->stop();
  for (auto &entry : clientEndpointMap_)
    if (entry.second)
      entry.second->stop();
}

void Server::run() {
  cout << "[SERVER]: Starting on port " << port_ << " (epoch=" << epoch_
       << ")...\n";
  running_ = true;

  // Signal readiness for healthcheck / orchestration.
  {
    std::ofstream ready("/tmp/squid_ready");
    ready << port_ << std::endl;
  }

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

  standbyHbThread_ = thread([this]() {
    while (running_) {
      {
        shared_lock<shared_mutex> lock(stateMutex_);
        if (standbyReplicaManager_)
          standbyReplicaManager_->sendHeartbeats();
      }
      this_thread::sleep_for(chrono::milliseconds(500));
    }
  });

  if (acceptThread_.joinable())
    acceptThread_.join();

  running_ = false;

  if (heartbeatThread_.joinable())
    heartbeatThread_.join();
  if (lockExpiryThread_.joinable())
    lockExpiryThread_.join();
  if (standbyHbThread_.joinable())
    standbyHbThread_.join();
}

void Server::handleAccept(AcceptedConnection accepted) {
  try {
  auto channel = accepted.channel;
  if (!channel)
    return;

  SquidProtocol proto(fileManager_, channel, "[SERVER]", "SERVER");

  Message mex = proto.identify();
  if (!proto.isAlive()) {
    cerr << "[SERVER]: Handshake failed — peer disconnected during IDENTIFY\n";
    return;
  }

  string peerProcessName = mex.getString(FieldID::PROCESS_NAME);
  string peerNodeType = mex.getString(FieldID::NODE_TYPE);

  if (peerNodeType != "DATANODE" && peerNodeType != "CLIENT" &&
      peerNodeType != "STANDBY") {
    cout << "[SERVER]: Unknown node type '" << peerNodeType << "' — dropping\n";
    return;
  }

  proto.response(true);
  if (!proto.isAlive()) {
    cerr << "[SERVER]: Handshake failed — could not send ACK to "
         << peerProcessName << "\n";
    return;
  }

  cout << "[SERVER]: Handshake complete with " << peerNodeType << " '"
       << peerProcessName << "'\n";

  if (peerNodeType == "STANDBY") {
    handleStandbyConnect(peerProcessName, channel);
    return;
  }

  if (peerNodeType == "DATANODE") {
    auto datanodeSession = make_shared<ConnectionSession>(
        fileManager_, channel, "DATANODE", peerProcessName);
    datanodeSession->start(false);

    {
      unique_lock<shared_mutex> lock(stateMutex_);
      dataNodeEndpointMap_[peerProcessName] = datanodeSession;
    }

    Message files = datanodeSession->syncStatus();
    if (files.isResponse()) {
      auto fileVersionMap = files.getFileVersionMap();
      replicationManager_.registerDataNodeFiles(
          peerProcessName, datanodeSession, fileVersionMap);
      lockManager_.buildFileLockMap();
    }
    return;
  }

  auto clientSession = make_shared<ConnectionSession>(
      fileManager_, channel, "CLIENT", peerProcessName,
      [this](ConnectionSession &session, const Message &message) {
        // handleClientRequest must NOT run on the session worker thread.
        // For CREATE_FILE and UPDATE_FILE the server sends an intermediate ACK
        // and then calls receiveFileData(), which blocks waiting for the client
        // to send the file bytes.  Those bytes arrive as raw TCP data that the
        // session worker must remain free to read.  If we call
        // handleClientRequest directly here (i.e. on the worker thread), the
        // worker is stuck inside receiveFileData() and can never drain the
        // incoming bytes — deadlock.  Dispatching to a separate pool thread
        // keeps the session worker free to process incoming data while the
        // handler is blocked on the receive.
        auto sessionPtr = session.shared_from_this();
        sessionPtr->suspendReads();
        requestPool_.submit([this, sessionPtr, message]() {
          handleClientRequest(*sessionPtr, message);
          sessionPtr->resumeReads();
        });
      });

  {
    unique_lock<shared_mutex> lock(stateMutex_);
    clientEndpointMap_[peerProcessName] = clientSession;
  }

  auto fileVersionMap = replicationManager_.getFileVersionMap();
  for (auto &[filePath, version] : fileVersionMap) {
    vector<uint8_t> fileData;
    if (replicationManager_.getFileFromDataNode(filePath, fileData))
      clientSession->pushCreateFile(filePath, version, fileData);
  }

  clientSession->start(true);
  } catch (const std::exception &e) {
    cerr << "[SERVER]: handleAccept failed: " << e.what() << "\n";
  }
}

void Server::handleStandbyConnect(const string &name,
                                   shared_ptr<INetworkChannel> channel) {
  {
    unique_lock<shared_mutex> lock(stateMutex_);
    if (!standbyReplicaManager_) {
      standbyReplicaManager_ = make_unique<StandbyReplicaManager>(
          fileManager_, replicationManager_, requestPool_, epoch_);
      replicationManager_.setStandbyReplicaManager(
          standbyReplicaManager_.get());
    }
  }

  auto session =
      make_shared<ConnectionSession>(fileManager_, channel, "STANDBY", name);
  session->start(false);

  standbyReplicaManager_->registerStandby(name, session);
}

void Server::handleClientRequest(ConnectionSession &clientSession,
                                 const Message &mex) {
  string filePath = mex.getString(FieldID::FILE_PATH);
  int fileVersion = static_cast<int>(mex.getUint32(FieldID::FILE_VERSION, 0));

  switch (mex.opcode) {
  case Opcode::CREATE_FILE: {
    vector<uint8_t> fileData;
    bool recvOk = clientSession.call([&fileData](SquidProtocol &proto) -> bool {
      proto.response(true);                   // ACK: "send the file"
      return proto.receiveFileData(fileData); // immediately consume raw bytes
    });
    bool ok =
        recvOk && replicationManager_.propagateCreateFile(
                      filePath, fileVersion, clientSession.getProcessName(),
                      fileData, lockManager_);
    clientSession.response(ok);
    break;
  }

  case Opcode::READ_FILE: {
    vector<uint8_t> fileData;
    bool ok = replicationManager_.getFileFromDataNode(filePath, fileData);
    if (ok) {
      int ver = replicationManager_.getKnownVersion(filePath);
      clientSession.response(true);
      clientSession.response(clientSession.sendFileData(fileData), ver);
    } else {
      clientSession.response(false);
    }
    break;
  }

  case Opcode::UPDATE_FILE: {
    vector<uint8_t> fileData;
    bool recvOk = clientSession.call([&fileData](SquidProtocol &proto) -> bool {
      proto.response(true);
      return proto.receiveFileData(fileData);
    });
    int newVersion = -1;
    if (recvOk)
      newVersion = replicationManager_.propagateUpdateFile(
          filePath, fileVersion, clientSession.getProcessName(), fileData);
    if (newVersion >= 0)
      clientSession.response(true, newVersion);
    else
      clientSession.response(false);
    break;
  }

  case Opcode::DELETE_FILE: {
    replicationManager_.propagateDeleteFile(
        filePath, clientSession.getProcessName(), lockManager_);
    clientSession.response(true);
    break;
  }

  case Opcode::SYNC_STATUS:
    clientSession.response(replicationManager_.getFileVersionMap());
    break;

  case Opcode::ACQUIRE_LOCK:
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
