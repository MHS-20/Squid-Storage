#pragma once

#include <chrono>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

#include "ConnectionSession.hpp"
#include "filelock.hpp"
#include "filemanager.hpp"
#include "peer.hpp"
#include "squidprotocol.hpp"

#define DEFAULT_LOCK_INTERVAL 5

// Owns the file lock map and all lock lifecycle operations:
// acquire, release, expiry checking, and rebuilding from datanodes.
class LockManager {
public:
  // shared_mutex and endpoint maps are owned by Server and passed by reference
  // so LockManager can coordinate with live connection state.
  LockManager(std::shared_mutex &stateMutex,
              std::map<std::string, std::shared_ptr<ConnectionSession>>
                  &dataNodeEndpointMap,
              std::map<std::string, std::shared_ptr<ConnectionSession>>
                  &clientEndpointMap,
              FileManager &fileManager);

  // Rebuild fileLockMap by querying all connected datanodes.
  void buildFileLockMap();

  // Try to acquire the lock for a file path.
  // Returns false if the file has no lock entry or is already locked.
  bool acquireLock(const std::string &path);

  // Release the lock for a file path.
  bool releaseLock(const std::string &path);

  // Called periodically; releases expired locks and notifies the owning client.
  void checkFileLockExpiration();

  // Insert a new unlocked entry (called after a file is successfully created).
  void insertLock(const std::string &filePath);

  // Remove a lock entry (called when a file is deleted).
  void eraseLock(const std::string &filePath);

private:
  std::shared_mutex &stateMutex_;
  std::map<std::string, std::shared_ptr<ConnectionSession>>
      &dataNodeEndpointMap_;
  std::map<std::string, std::shared_ptr<ConnectionSession>> &clientEndpointMap_;
  FileManager &fileManager_;

  std::map<std::string, FileLock> fileLockMap_;

  std::shared_ptr<ConnectionSession> getClientSession(const std::string &name);
};
