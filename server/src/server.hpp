#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>

#include "filemanager.hpp"
#include "filetransfer.hpp"
#include "heartbeat_manager.hpp"
#include "lock_manager.hpp"
#include "networking/TCPListenerChannel.hpp"
#include "replication_manager.hpp"
#include "squidprotocol.hpp"
#include "thread_pool.hpp"

#define DEFAULT_PORT 12345
#define DEFAULT_TIMEOUT 60
#define DEFAULT_REPLICATION_FACTOR 2

class Server {
public:
  Server();
  explicit Server(int port);
  Server(int port, int replicationFactor);
  Server(int port, int replicationFactor, int timeoutSeconds);
  ~Server();

  void run();

private:
  int port_;
  int replicationFactor_;
  std::atomic<bool> running_{false};

  std::unique_ptr<TCPListenerChannel> listener_;
  ThreadPool requestPool_;

  std::thread acceptThread_;
  std::thread heartbeatThread_;
  std::thread lockExpiryThread_;

  FileTransfer fileTransfer_;
  FileManager fileManager_;

  // Shared state — all access must hold stateMutex_.
  mutable std::shared_mutex stateMutex_;
  std::map<std::string, std::shared_ptr<ConnectionSession>> dataNodeEndpointMap_;
  std::map<std::string, std::shared_ptr<ConnectionSession>> clientEndpointMap_;

  // Sub-managers: constructed after the shared maps above (member order matters).
  LockManager lockManager_;
  ReplicationManager replicationManager_;
  HeartbeatManager heartbeatManager_;

  // handleAccept is offloaded to requestPool_ so the accept thread is never
  // blocked by handshake or initial sync work.
  void handleAccept(AcceptedConnection accepted);
  void handleClientRequest(ConnectionSession &clientSession,
                           const Message &message);

  std::shared_ptr<ConnectionSession> getClientSession(const std::string &name);
  std::shared_ptr<ConnectionSession> getDataNodeSession(const std::string &name);

  void printMap(std::map<std::string, long long> &m, const std::string &name);
  void printMap(std::map<std::string, std::shared_ptr<ConnectionSession>> &m,
                const std::string &name);
};
