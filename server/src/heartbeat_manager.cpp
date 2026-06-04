#include "heartbeat_manager.hpp"
#include "replication_manager.hpp"
#include "peer.hpp"

#include <iostream>
#include <mutex>
#include <vector>

using namespace std;

HeartbeatManager::HeartbeatManager(
    shared_mutex &stateMutex,
    map<string, shared_ptr<ConnectionSession>> &dataNodeEndpointMap,
    ThreadPool &requestPool,
    ReplicationManager &replicationManager)
    : stateMutex_(stateMutex),
      dataNodeEndpointMap_(dataNodeEndpointMap),
      requestPool_(requestPool),
      replicationManager_(replicationManager) {}

void HeartbeatManager::sendHeartbeats() {
  vector<pair<string, shared_ptr<ConnectionSession>>> datanodes;
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    for (auto &entry : dataNodeEndpointMap_)
      datanodes.push_back(entry);
  }

  vector<string> deadNodes;
  vector<future<Message>> futures;
  futures.reserve(datanodes.size());

  for (auto &entry : datanodes) {
    if (!entry.second)
      continue;
    futures.push_back(requestPool_.submit(
        [session = entry.second]() { return session->heartbeat(); }));
  }

  for (size_t i = 0; i < datanodes.size(); ++i) {
    auto &entry = datanodes[i];
    if (!entry.second)
      continue;

    Message heartbeat = futures[i].get();
    if (!heartbeat.isAck()) {
      entry.second->setIsAlive(false);
      deadNodes.push_back(entry.first);
    }
  }

  if (!deadNodes.empty()) {
    unique_lock<shared_mutex> lock(stateMutex_);
    for (auto &name : deadNodes)
      dataNodeEndpointMap_.erase(name);
  }

  replicationManager_.eraseFromReplicationMap(deadNodes);
}
