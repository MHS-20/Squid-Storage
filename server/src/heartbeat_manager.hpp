#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "ConnectionSession.hpp"
#include "peer.hpp"
#include "thread_pool.hpp"

class ReplicationManager;

// Periodically pings all connected datanodes, evicts dead ones, and triggers
// replication rebalancing via ReplicationManager.
class HeartbeatManager {
public:
  HeartbeatManager(std::shared_mutex &stateMutex,
                   std::map<std::string, std::shared_ptr<ConnectionSession>>
                       &dataNodeEndpointMap,
                   ThreadPool &requestPool,
                   ReplicationManager &replicationManager);

  // Send heartbeats to all datanodes; evict and rebalance any that fail.
  void sendHeartbeats();

private:
  std::shared_mutex &stateMutex_;
  std::map<std::string, std::shared_ptr<ConnectionSession>>
      &dataNodeEndpointMap_;
  ThreadPool &requestPool_;
  ReplicationManager &replicationManager_;
};
