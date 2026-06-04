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

using namespace std;

class Server {
public:
  Server();
  Server(int port);
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

  // Shared state protected by stateMutex_
  mutable std::shared_mutex stateMutex_;
  map<string, shared_ptr<ConnectionSession>> dataNodeEndpointMap_;
  map<string, shared_ptr<ConnectionSession>> clientEndpointMap_;

  // Sub-managers: constructed after the shared maps above.
  LockManager lockManager_;
  ReplicationManager replicationManager_;
  HeartbeatManager heartbeatManager_;

  void handleAccept(AcceptedConnection accepted);
  void handleConnection(SquidProtocol &clientProtocol);
  void handleClientRequest(ConnectionSession &clientSession,
                           const Message &message);

  shared_ptr<ConnectionSession> getClientSession(const string &name);
  shared_ptr<ConnectionSession> getDataNodeSession(const string &name);

  void printMap(map<string, long long> &m, const string &name);
  void printMap(map<string, shared_ptr<ConnectionSession>> &m,
                const string &name);
};
