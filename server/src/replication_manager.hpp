#pragma once

#include <future>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "ConnectionSession.hpp"
#include "filemanager.hpp"
#include "peer.hpp"
#include "squidprotocol.hpp"
#include "thread_pool.hpp"

class LockManager;

// Owns the datanode replication map and all file-propagation operations:
// create/update/delete fan-out to datanodes and cache-invalidation to clients,
// rebalancing when a datanode is lost, and fetching file data from datanodes.
class ReplicationManager {
public:
  ReplicationManager(int replicationFactor, std::shared_mutex &stateMutex,
                     std::map<std::string, std::shared_ptr<ConnectionSession>>
                         &dataNodeEndpointMap,
                     std::map<std::string, std::shared_ptr<ConnectionSession>>
                         &clientEndpointMap,
                     FileManager &fileManager, ThreadPool &requestPool);

  // Populate the replication map from a freshly-connected datanode.
  // Called by LockManager::buildFileLockMap (pass the session map entry).
  void registerDataNodeFiles(const std::string &datanodeName,
                             std::shared_ptr<ConnectionSession> datanodeSession,
                             const std::map<std::string, int> &fileVersionMap);

  // Retrieve file bytes from any live holder datanode.
  bool getFileFromDataNode(const std::string &filePath,
                           std::vector<uint8_t> &fileData);

  // Fan-out create/update/delete to all relevant datanodes and notify clients.
  bool propagateCreateFile(const std::string &filePath, int version,
                           const std::string &originProcessName,
                           const std::vector<uint8_t> &fileData,
                           LockManager &lockManager);

  bool propagateUpdateFile(const std::string &filePath, int version,
                           const std::string &originProcessName,
                           const std::vector<uint8_t> &fileData);

  void propagateDeleteFile(const std::string &filePath,
                           const std::string &originProcessName,
                           LockManager &lockManager);

  // Remove a dead datanode from the replication map and trigger rebalancing.
  void eraseFromReplicationMap(const std::vector<std::string> &datanodeNames);
  void eraseFromReplicationMap(const std::string &datanodeName);

  // Return the highest known version for every file across all live datanodes.
  std::map<std::string, int> getFileVersionMap();

private:
  int replicationFactor_;
  std::shared_mutex &stateMutex_;
  std::map<std::string, std::shared_ptr<ConnectionSession>>
      &dataNodeEndpointMap_;
  std::map<std::string, std::shared_ptr<ConnectionSession>> &clientEndpointMap_;
  FileManager &fileManager_;
  ThreadPool &requestPool_;

  // filePath -> { datanodeName -> session }
  std::map<std::string,
           std::map<std::string, std::shared_ptr<ConnectionSession>>>
      dataNodeReplicationMap_;

  void rebalanceFileReplication(
      const std::string &filePath,
      std::map<std::string, std::shared_ptr<ConnectionSession>> fileHoldersMap);

  std::vector<std::string> pickDataNodes(size_t count);

  size_t roundRobinCursor_ = 0;
};
