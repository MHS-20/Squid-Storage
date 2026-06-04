#include "replication_manager.hpp"
#include "lock_manager.hpp"
#include "peer.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>

using namespace std;

ReplicationManager::ReplicationManager(
    int replicationFactor,
    shared_mutex &stateMutex,
    map<string, shared_ptr<ConnectionSession>> &dataNodeEndpointMap,
    map<string, shared_ptr<ConnectionSession>> &clientEndpointMap,
    FileManager &fileManager,
    ThreadPool &requestPool)
    : replicationFactor_(replicationFactor),
      stateMutex_(stateMutex),
      dataNodeEndpointMap_(dataNodeEndpointMap),
      clientEndpointMap_(clientEndpointMap),
      fileManager_(fileManager),
      requestPool_(requestPool) {}

void ReplicationManager::registerDataNodeFiles(
    const string &datanodeName,
    shared_ptr<ConnectionSession> datanodeSession,
    const map<string, int> &fileVersionMap) {
  unique_lock<shared_mutex> lock(stateMutex_);
  for (auto &file : fileVersionMap)
    dataNodeReplicationMap_[file.first][datanodeName] = datanodeSession;
}

bool ReplicationManager::getFileFromDataNode(const string &filePath,
                                             vector<uint8_t> &fileData) {
  shared_ptr<ConnectionSession> dataNodeHolder;
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    auto it = dataNodeReplicationMap_.find(filePath);
    if (it == dataNodeReplicationMap_.end())
      return false;

    for (auto &datanode : it->second) {
      if (datanode.second && datanode.second->isAlive()) {
        dataNodeHolder = datanode.second;
        break;
      }
    }
  }

  if (!dataNodeHolder)
    return false;

  Message mex = dataNodeHolder->readFile(filePath, fileData);
  return mex.isAck();
}

bool ReplicationManager::propagateCreateFile(
    const string &filePath, int version, const string &originProcessName,
    const vector<uint8_t> &fileData, LockManager &lockManager) {
  vector<pair<string, shared_ptr<ConnectionSession>>> datanodes;
  vector<pair<string, shared_ptr<ConnectionSession>>> clients;

  {
    shared_lock<shared_mutex> lock(stateMutex_);
    for (auto &entry : dataNodeEndpointMap_)
      datanodes.push_back(entry);
    for (auto &entry : clientEndpointMap_)
      clients.push_back(entry);
  }

  bool ok = true;
  vector<future<Message>> futures;
  futures.reserve(datanodes.size());

  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;

    futures.push_back(requestPool_.submit(
        [session = datanode.second, filePath, version, fileData]() {
          return session->createFile(filePath, version, fileData);
        }));
  }

  size_t futureIndex = 0;
  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;

    Message response = futures[futureIndex++].get();
    if (!response.isAck()) {
      ok = false;
    } else {
      unique_lock<shared_mutex> lock(stateMutex_);
      dataNodeReplicationMap_[filePath][datanode.first] = datanode.second;
    }
  }

  if (ok)
    lockManager.insertLock(filePath);

  for (auto &client : clients) {
    if (!client.second || client.first == originProcessName)
      continue;
    client.second->pushCreateFile(filePath, version, fileData);
  }

  if (ok)
    fileManager_.setFileVersion(filePath, version);

  return ok;
}

bool ReplicationManager::propagateUpdateFile(const string &filePath,
                                             int version,
                                             const string &originProcessName,
                                             const vector<uint8_t> &fileData) {
  vector<pair<string, shared_ptr<ConnectionSession>>> datanodes;
  vector<pair<string, shared_ptr<ConnectionSession>>> clients;

  {
    shared_lock<shared_mutex> lock(stateMutex_);
    auto it = dataNodeReplicationMap_.find(filePath);
    if (it != dataNodeReplicationMap_.end()) {
      for (auto &entry : it->second)
        datanodes.push_back(entry);
    }
    for (auto &entry : clientEndpointMap_)
      clients.push_back(entry);
  }

  bool ok = true;
  vector<future<Message>> futures;
  futures.reserve(datanodes.size());

  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;

    futures.push_back(requestPool_.submit(
        [session = datanode.second, filePath, version, fileData]() {
          return session->updateFile(filePath, version, fileData);
        }));
  }

  size_t futureIndex = 0;
  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;

    Message response = futures[futureIndex++].get();
    if (!response.isAck())
      ok = false;
  }

  for (auto &client : clients) {
    if (!client.second || client.first == originProcessName)
      continue;
    client.second->pushUpdateFile(filePath, version, fileData);
  }

  if (ok)
    fileManager_.setFileVersion(filePath, version);

  return ok;
}

void ReplicationManager::propagateDeleteFile(const string &filePath,
                                             const string &originProcessName,
                                             LockManager &lockManager) {
  vector<pair<string, shared_ptr<ConnectionSession>>> datanodes;
  vector<pair<string, shared_ptr<ConnectionSession>>> clients;

  {
    shared_lock<shared_mutex> lock(stateMutex_);
    auto it = dataNodeReplicationMap_.find(filePath);
    if (it != dataNodeReplicationMap_.end()) {
      for (auto &entry : it->second)
        datanodes.push_back(entry);
    }
    for (auto &entry : clientEndpointMap_)
      clients.push_back(entry);
  }

  for (auto &datanode : datanodes) {
    if (datanode.second && datanode.second->isAlive())
      requestPool_.submit([session = datanode.second, filePath]() {
        return session->deleteFile(filePath);
      });
  }

  for (auto &client : clients) {
    if (!client.second || client.first == originProcessName)
      continue;
    client.second->post(
        [filePath](SquidProtocol &protocol) { protocol.deleteFile(filePath); });
  }

  lockManager.eraseLock(filePath);

  {
    unique_lock<shared_mutex> lock(stateMutex_);
    dataNodeReplicationMap_.erase(filePath);
  }

  fileManager_.deleteFileAndVersion(filePath);
}

void ReplicationManager::eraseFromReplicationMap(
    const vector<string> &datanodeNames) {
  for (auto &name : datanodeNames)
    eraseFromReplicationMap(name);
}

void ReplicationManager::eraseFromReplicationMap(const string &datanodeName) {
  vector<pair<string, map<string, shared_ptr<ConnectionSession>>>>
      rebalanceTargets;
  {
    unique_lock<shared_mutex> lock(stateMutex_);
    for (auto it = dataNodeReplicationMap_.begin();
         it != dataNodeReplicationMap_.end(); ++it) {
      auto endpoint = it->second.find(datanodeName);
      if (endpoint == it->second.end())
        continue;

      it->second.erase(endpoint->first);
      if (it->second.size() < static_cast<size_t>((replicationFactor_ / 2) + 1))
        rebalanceTargets.push_back({it->first, it->second});
    }
  }

  for (auto &target : rebalanceTargets)
    rebalanceFileReplication(target.first, target.second);
}

void ReplicationManager::rebalanceFileReplication(
    const string &filePath,
    map<string, shared_ptr<ConnectionSession>> fileHoldersMap) {
  vector<uint8_t> fileData;
  if (fileHoldersMap.empty())
    return;

  auto sourceSession = fileHoldersMap.begin()->second;
  if (!sourceSession)
    return;

  Message sourceMessage = sourceSession->readFile(filePath, fileData);
  if (!sourceMessage.isAck())
    return;

  vector<pair<string, shared_ptr<ConnectionSession>>> candidates;
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    for (auto &entry : dataNodeEndpointMap_) {
      if (fileHoldersMap.find(entry.first) == fileHoldersMap.end() &&
          entry.second && entry.second->isAlive())
        candidates.push_back(entry);
    }
  }

  for (auto &candidate : candidates) {
    Message response = candidate.second->createFile(
        filePath, fileManager_.getFileVersion(filePath), fileData);
    if (response.isAck()) {
      fileHoldersMap[candidate.first] = candidate.second;
      if (fileHoldersMap.size() >= static_cast<size_t>(replicationFactor_))
        break;
    }
  }

  {
    unique_lock<shared_mutex> lock(stateMutex_);
    dataNodeReplicationMap_[filePath] = std::move(fileHoldersMap);
  }
}

map<string, int> ReplicationManager::getFileVersionMap() {
  map<string, int> fileVersionMap;
  vector<shared_ptr<ConnectionSession>> datanodes;
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    for (auto &entry : dataNodeEndpointMap_)
      datanodes.push_back(entry.second);
  }

  for (auto &datanode : datanodes) {
    if (!datanode || !datanode->isAlive())
      continue;

    Message mex = datanode->listFiles();
    map<string, int> datanodeMap = mex.getFileVersionMap();
    for (auto &file : datanodeMap) {
      auto it = fileVersionMap.find(file.first);
      if (it == fileVersionMap.end())
        fileVersionMap[file.first] = file.second;
      else
        it->second = max(it->second, file.second);
    }
  }

  return fileVersionMap;
}

vector<string> ReplicationManager::pickDataNodes(size_t count) {
  shared_lock<shared_mutex> lock(stateMutex_);
  vector<string> nodes;
  nodes.reserve(dataNodeEndpointMap_.size());
  for (auto &entry : dataNodeEndpointMap_)
    nodes.push_back(entry.first);

  vector<string> selected;
  if (nodes.empty())
    return selected;

  roundRobinCursor_ %= nodes.size();
  for (size_t i = 0; i < count && i < nodes.size(); ++i)
    selected.push_back(nodes[(roundRobinCursor_ + i) % nodes.size()]);

  roundRobinCursor_ = (roundRobinCursor_ + count) % nodes.size();
  return selected;
}
