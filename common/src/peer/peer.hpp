#pragma once
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ClusterConfig.hpp"
#include "ConnectionSession.hpp"
#include "filelock.hpp"
#include "filesystem/filemanager.hpp"
#include "filetransfer.hpp"
#include "networking/TCPConnectorChannel.hpp"
#include "squidprotocol.hpp"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345

class Peer {
  friend class SquidFS;

public:
  Peer();
  Peer(std::string nodeType, std::string processName);
  Peer(int port, std::string nodeType, std::string processName);
  Peer(std::string server_ip, int port, std::string nodeType,
       std::string processName);
  virtual ~Peer();

  virtual void connectToServer();
  virtual void connectWithFailover(const ClusterConfig &config);
  virtual void reconnect();
  virtual void disconnect();
  virtual bool isAlive() const;

  virtual void run() = 0;

  // Thread-safe copy of session_ (returns nullptr if no session).
  std::shared_ptr<ConnectionSession> lockedSession() const {
    std::lock_guard<std::mutex> lk(sessionMutex_);
    return session_;
  }

protected:
  int port = SERVER_PORT;
  std::string server_ip = SERVER_IP;
  int timeoutSeconds = 60;

  std::string nodeType_;
  std::string processName_;

  FileManager fileManager_;
  FileTransfer fileTransfer_;

  mutable std::mutex sessionMutex_;
  std::shared_ptr<ConnectionSession> session_;

  uint32_t lastSeenEpoch_ = 0;
  ClusterConfig clusterConfig_;

  virtual ConnectionSession::RequestHandler makeRequestHandler() = 0;
  virtual void onConnected() {}

private:
  std::shared_ptr<INetworkChannel> tryConnectChannel(const std::string &ip,
                                                     int port);
  bool performHandshake(std::shared_ptr<INetworkChannel> channel);
};
