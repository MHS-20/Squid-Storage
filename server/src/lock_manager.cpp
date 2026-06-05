#include "lock_manager.hpp"

#include "peer.hpp"
#include "squidprotocol.hpp"

#include <iostream>
#include <mutex>

using namespace std;

LockManager::LockManager(
    shared_mutex &stateMutex,
    map<string, shared_ptr<ConnectionSession>> &dataNodeEndpointMap,
    map<string, shared_ptr<ConnectionSession>> &clientEndpointMap,
    FileManager &fileManager)
    : stateMutex_(stateMutex), dataNodeEndpointMap_(dataNodeEndpointMap),
      clientEndpointMap_(clientEndpointMap), fileManager_(fileManager) {}

void LockManager::buildFileLockMap() {
  vector<shared_ptr<ConnectionSession>> datanodes;
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    for (auto &entry : dataNodeEndpointMap_)
      datanodes.push_back(entry.second);
  }

  for (auto &datanodeSession : datanodes) {
    if (!datanodeSession || !datanodeSession->isAlive())
      continue;

    Message files = datanodeSession->syncStatus();
    if (!files.isResponse())
      continue;

    map<string, int> fileMap = files.getFileVersionMap();
    unique_lock<shared_mutex> lock(stateMutex_);
    for (auto &file : fileMap) {
      if (fileLockMap_.find(file.first) == fileLockMap_.end())
        fileLockMap_[file.first] = FileLock(file.first);

      int localVersion = fileManager_.getFileVersion(file.first);
      if (localVersion < file.second)
        fileManager_.setFileVersion(file.first, file.second);
    }
  }
}

bool LockManager::acquireLock(const string &path, const string &clientName) {
  unique_lock<shared_mutex> lock(stateMutex_);
  if (fileLockMap_.find(path) == fileLockMap_.end()) {
    lock.unlock();
    buildFileLockMap();
    lock.lock();
    if (fileLockMap_.find(path) == fileLockMap_.end())
      return false;
  }

  if (!fileLockMap_[path].isLocked()) {
    fileLockMap_[path].setIsLocked(true);
    fileLockMap_[path].setClientHolder(clientName);
    fileLockMap_[path].setExpiration(chrono::system_clock::now() +
                                     chrono::minutes(DEFAULT_LOCK_INTERVAL));
    return true;
  }

  return false;
}

bool LockManager::releaseLock(const string &path) {
  unique_lock<shared_mutex> lock(stateMutex_);
  if (fileLockMap_.find(path) == fileLockMap_.end()) {
    lock.unlock();
    buildFileLockMap();
    lock.lock();
    if (fileLockMap_.find(path) == fileLockMap_.end())
      return false;
    return false;
  }

  fileLockMap_[path].setIsLocked(false);
  return true;
}

void LockManager::checkFileLockExpiration() {
  vector<pair<string, string>> expiredLocks;
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    auto now = chrono::system_clock::now();
    for (auto &entry : fileLockMap_) {
      if (entry.second.isLocked() && entry.second.getExpiration() < now)
        expiredLocks.emplace_back(entry.first, entry.second.getClientHolder());
    }
  }

  for (auto &expired : expiredLocks) {
    auto clientSession = getClientSession(expired.second);
    if (clientSession)
      clientSession->post(
          [expiredFile = expired.first](SquidProtocol &protocol) {
            protocol.releaseLock(expiredFile);
          });

    unique_lock<shared_mutex> lock(stateMutex_);
    auto it = fileLockMap_.find(expired.first);
    if (it != fileLockMap_.end())
      it->second.setIsLocked(false);
  }
}

void LockManager::insertLock(const string &filePath) {
  unique_lock<shared_mutex> lock(stateMutex_);
  fileLockMap_.insert({filePath, FileLock(filePath)});
}

void LockManager::eraseLock(const string &filePath) {
  unique_lock<shared_mutex> lock(stateMutex_);
  fileLockMap_.erase(filePath);
}

shared_ptr<ConnectionSession>
LockManager::getClientSession(const string &name) {
  shared_lock<shared_mutex> lock(stateMutex_);
  auto it = clientEndpointMap_.find(name);
  return it == clientEndpointMap_.end() ? nullptr : it->second;
}
