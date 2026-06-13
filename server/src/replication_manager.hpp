#pragma once

#include <future>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <vector>

#include "ConnectionSession.hpp"
#include "filemanager.hpp"
#include "peer.hpp"
#include "squidprotocol.hpp"
#include "state_manager.hpp"
#include "thread_pool.hpp"

class LockManager;
class StandbyReplicaManager;

// Owns the datanode replication map and all file-propagation operations:
// create/update/delete fan-out to datanodes and cache-invalidation to clients,
// rebalancing when a datanode is lost, and fetching file data from datanodes.
//
// Write quorum: a write is committed only when at least
//   quorum_ = (replicationFactor / 2) + 1
// datanodes ACK.  If fewer ACK, the write is rolled back and the client is
// NACKed.
//
// Read quorum: on a normal read we serve from the first live holder that
// reports the expected version.  If that holder's version is lower than
// what the server tracks, we query all holders and return the highest
// version available; if no holder has the expected version we return an error.
class ReplicationManager {
public:
  ReplicationManager(int replicationFactor, std::shared_mutex &stateMutex,
                     std::map<std::string, std::shared_ptr<ConnectionSession>>
                         &dataNodeEndpointMap,
                     std::map<std::string, std::shared_ptr<ConnectionSession>>
                         &clientEndpointMap,
                     FileManager &fileManager, ThreadPool &requestPool);

  // Populate the replication map from a freshly-connected datanode and
  // reconcile versions against the persisted state.  Called by
  // LockManager::buildFileLockMap (pass the session map entry).
  void registerDataNodeFiles(const std::string &datanodeName,
                             std::shared_ptr<ConnectionSession> datanodeSession,
                             const std::map<std::string, int> &fileVersionMap);

  // Retrieve file bytes from any live holder datanode.
  // Applies read-quorum logic when the first holder is stale.
  bool getFileFromDataNode(const std::string &filePath,
                           std::vector<uint8_t> &fileData);

  // Fan-out create/update/delete to all relevant datanodes and notify clients.
  bool propagateCreateFile(const std::string &filePath, int version,
                           const std::string &originProcessName,
                           const std::vector<uint8_t> &fileData,
                           LockManager &lockManager);

  // Returns the new committed version, or -1 if quorum was not met.
  int propagateUpdateFile(const std::string &filePath, int clientSeenVersion,
                          const std::string &originProcessName,
                          const std::vector<uint8_t> &fileData);

  void propagateDeleteFile(const std::string &filePath,
                           const std::string &originProcessName,
                           LockManager &lockManager);

  // Remove a dead datanode from the replication map and trigger rebalancing.
  // Also persists the updated replication map.
  void eraseFromReplicationMap(const std::vector<std::string> &datanodeNames);
  void eraseFromReplicationMap(const std::string &datanodeName);

  // Return the highest known version for every file across all live datanodes.
  std::map<std::string, int> getFileVersionMap();

  // Return the server's current known version for a single file (-1 if unknown).
  int getKnownVersion(const std::string &filePath);

  // Return the current replication map in a form suitable for snapshotting.
  std::map<std::string, std::set<std::string>> getPersistableRepMap();

  // Wire in the standby fanout after construction (avoids circular dependency
  // between ReplicationManager and StandbyReplicaManager).
  void setStandbyReplicaManager(StandbyReplicaManager *srm) {
    standby_.store(srm, std::memory_order_release);
  }

private:
  int replicationFactor_;
  size_t quorum_;              // (replicationFactor / 2) + 1
  std::shared_mutex &stateMutex_;
  std::map<std::string, std::shared_ptr<ConnectionSession>>
      &dataNodeEndpointMap_;
  std::map<std::string, std::shared_ptr<ConnectionSession>> &clientEndpointMap_;
  FileManager &fileManager_;
  ThreadPool &requestPool_;
  StateManager stateManager_;

  // filePath -> { datanodeName -> session }
  std::map<std::string,
           std::map<std::string, std::shared_ptr<ConnectionSession>>>
      dataNodeReplicationMap_;

  // Persisted version map (authoritative when datanodes are offline).
  std::map<std::string, int> persistedVersionMap_;

  // Optional standby fanout — null when running without HA config.
  // Atomic for lock-free reads from propagation threads; written once by
  // setStandbyReplicaManager() during a standby connect.
  std::atomic<StandbyReplicaManager*> standby_{nullptr};

  // Persist both state files atomically.
  void persistState();

  // Build the set-of-names replication map for StateManager from the live map.
  std::map<std::string, std::set<std::string>> buildPersistableRepMap() const;

  void rebalanceFileReplication(
      const std::string &filePath,
      std::map<std::string, std::shared_ptr<ConnectionSession>> fileHoldersMap);

  std::vector<std::string> pickDataNodes(size_t count);

  std::atomic<size_t> roundRobinCursor_{0};
};
