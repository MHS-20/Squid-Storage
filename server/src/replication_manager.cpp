#include "replication_manager.hpp"
#include "lock_manager.hpp"
#include "peer.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>

using namespace std;

// ── Constructor ───────────────────────────────────────────────────────────────

ReplicationManager::ReplicationManager(
    int replicationFactor,
    shared_mutex &stateMutex,
    map<string, shared_ptr<ConnectionSession>> &dataNodeEndpointMap,
    map<string, shared_ptr<ConnectionSession>> &clientEndpointMap,
    FileManager &fileManager,
    ThreadPool &requestPool)
    : replicationFactor_(replicationFactor),
      quorum_(static_cast<size_t>(replicationFactor / 2) + 1),
      stateMutex_(stateMutex),
      dataNodeEndpointMap_(dataNodeEndpointMap),
      clientEndpointMap_(clientEndpointMap),
      fileManager_(fileManager),
      requestPool_(requestPool),
      stateManager_(fileManager) {
  // Load persisted state on startup.  Datanode reconciliation happens later
  // in registerDataNodeFiles (highest-version-wins).
  persistedVersionMap_ = stateManager_.loadVersionMap();
  cout << "[ReplicationManager]: loaded " << persistedVersionMap_.size()
       << " file version(s) from persistent state\n";

  auto repMap = stateManager_.loadReplicationMap();
  cout << "[ReplicationManager]: loaded replication map for "
       << repMap.size() << " file(s) from persistent state\n";
  // We don't restore live sessions here — that happens as datanodes reconnect.
}

// ── Helpers ───────────────────────────────────────────────────────────────────

map<string, set<string>> ReplicationManager::buildPersistableRepMap() const {
  map<string, set<string>> result;
  for (auto &[filePath, holders] : dataNodeReplicationMap_) {
    set<string> names;
    for (auto &[name, _] : holders)
      names.insert(name);
    result[filePath] = move(names);
  }
  return result;
}

void ReplicationManager::persistState() {
  stateManager_.saveVersionMap(persistedVersionMap_);
  stateManager_.saveReplicationMap(buildPersistableRepMap());
}

// ── Registration ──────────────────────────────────────────────────────────────

void ReplicationManager::registerDataNodeFiles(
    const string &datanodeName,
    shared_ptr<ConnectionSession> datanodeSession,
    const map<string, int> &fileVersionMap) {
  unique_lock<shared_mutex> lock(stateMutex_);

  for (auto &[filePath, datanodeVersion] : fileVersionMap) {
    // Register this datanode as a holder.
    dataNodeReplicationMap_[filePath][datanodeName] = datanodeSession;

    // Reconcile: highest version seen across live datanodes and persisted map.
    auto it = persistedVersionMap_.find(filePath);
    int currentBest = (it != persistedVersionMap_.end()) ? it->second : 0;
    if (datanodeVersion > currentBest) {
      persistedVersionMap_[filePath] = datanodeVersion;
      fileManager_.setFileVersion(filePath, datanodeVersion);
    }
  }

  // Also check if the persisted map mentions files this datanode doesn't have
  // yet — those will be pushed to it during the caller's reconciliation loop
  // (handled in Server::handleAccept / LockManager::buildFileLockMap).

  persistState();
}

// ── Read with quorum ──────────────────────────────────────────────────────────

bool ReplicationManager::getFileFromDataNode(const string &filePath,
                                             vector<uint8_t> &fileData) {
  // Collect all live holder sessions and the expected (persisted) version.
  vector<shared_ptr<ConnectionSession>> holders;
  int expectedVersion = -1;
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    auto it = dataNodeReplicationMap_.find(filePath);
    if (it == dataNodeReplicationMap_.end())
      return false;
    for (auto &[name, session] : it->second)
      if (session && session->isAlive())
        holders.push_back(session);

    auto vit = persistedVersionMap_.find(filePath);
    if (vit != persistedVersionMap_.end())
      expectedVersion = vit->second;
  }

  if (holders.empty())
    return false;

  // Fast path: ask the first holder what version it has via syncStatus().
  // If it matches (or we have no expectation), read from it directly.
  auto holderVersionOf = [&](shared_ptr<ConnectionSession> &session) -> int {
    Message statusMsg = session->syncStatus();
    auto vmap = statusMsg.getFileVersionMap();
    auto it = vmap.find(filePath);
    return (it != vmap.end()) ? it->second : -1;
  };

  int firstVersion = holderVersionOf(holders[0]);
  if (expectedVersion < 0 || firstVersion >= expectedVersion) {
    vector<uint8_t> candidate;
    Message mex = holders[0]->readFile(filePath, candidate);
    if (mex.isAck()) {
      fileData = move(candidate);
      return true;
    }
  } else {
    cerr << "[ReplicationManager]: stale read from first holder (v"
         << firstVersion << " < expected v" << expectedVersion
         << ") — querying remaining holders\n";
  }

  // Quorum read: find the holder with the highest version among the rest.
  int bestVersion = firstVersion;
  shared_ptr<ConnectionSession> bestHolder;

  for (size_t i = 1; i < holders.size(); ++i) {
    int ver = holderVersionOf(holders[i]);
    if (ver > bestVersion) {
      bestVersion = ver;
      bestHolder = holders[i];
    }
    if (ver >= expectedVersion)
      break; // good enough
  }

  if (!bestHolder && bestVersion < expectedVersion) {
    cerr << "[ReplicationManager]: read quorum failed for " << filePath
         << " — no holder has version >= " << expectedVersion << "\n";
    return false;
  }

  // Use best holder found, or fall back to first holder if none was better.
  auto &readFrom = bestHolder ? bestHolder : holders[0];
  vector<uint8_t> candidate;
  Message mex = readFrom->readFile(filePath, candidate);
  if (!mex.isAck())
    return false;

  fileData = move(candidate);
  return true;
}

// ── Write propagation ─────────────────────────────────────────────────────────

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

  // Fan out to all live datanodes in parallel.
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

  // Collect ACKs — track which datanodes succeeded.
  vector<pair<string, shared_ptr<ConnectionSession>>> ackedNodes;
  size_t futureIndex = 0;
  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;
    Message response = futures[futureIndex++].get();
    if (response.isAck())
      ackedNodes.push_back(datanode);
    else
      cerr << "[ReplicationManager]: CREATE_FILE NACK from " << datanode.first << "\n";
  }

  // Write-quorum check.
  if (ackedNodes.size() < quorum_) {
    cerr << "[ReplicationManager]: write quorum not met for CREATE "
         << filePath << " (" << ackedNodes.size() << "/" << quorum_
         << " ACKs) — rolling back\n";

    // Rollback: tell the nodes that ACK'd to delete this version.
    for (auto &node : ackedNodes) {
      requestPool_.submit([session = node.second, filePath]() {
        session->deleteFile(filePath);
      });
    }
    return false;
  }

  // Quorum met — register holders and persist state.
  {
    unique_lock<shared_mutex> lock(stateMutex_);
    for (auto &node : ackedNodes)
      dataNodeReplicationMap_[filePath][node.first] = node.second;
    persistedVersionMap_[filePath] = version;
  }

  lockManager.insertLock(filePath);
  fileManager_.setFileVersion(filePath, version);
  persistState();

  // Push to all other clients.
  for (auto &client : clients) {
    if (!client.second || client.first == originProcessName)
      continue;
    client.second->pushCreateFile(filePath, version, fileData);
  }

  return true;
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

  // Fan out in parallel.
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

  // Collect ACKs.
  vector<pair<string, shared_ptr<ConnectionSession>>> ackedNodes;
  size_t futureIndex = 0;
  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;
    Message response = futures[futureIndex++].get();
    if (response.isAck())
      ackedNodes.push_back(datanode);
    else
      cerr << "[ReplicationManager]: UPDATE_FILE NACK from " << datanode.first << "\n";
  }

  // Write-quorum check.
  // For update the quorum is against the holders of this file, not all nodes.
  size_t effectiveQuorum = min(quorum_, datanodes.size());
  if (ackedNodes.size() < effectiveQuorum) {
    cerr << "[ReplicationManager]: write quorum not met for UPDATE "
         << filePath << " (" << ackedNodes.size() << "/" << effectiveQuorum
         << " ACKs) — rejecting\n";
    return false;
  }

  // Quorum met.
  {
    unique_lock<shared_mutex> lock(stateMutex_);
    persistedVersionMap_[filePath] = version;
  }

  fileManager_.setFileVersion(filePath, version);
  persistState();

  // Push to all other clients.
  for (auto &client : clients) {
    if (!client.second || client.first == originProcessName)
      continue;
    client.second->pushUpdateFile(filePath, version, fileData);
  }

  return true;
}

void ReplicationManager::propagateDeleteFile(const string &filePath,
                                             const string &originProcessName,
                                             LockManager &lockManager) {
  vector<pair<string, shared_ptr<ConnectionSession>>> datanodes;
  vector<pair<string, shared_ptr<ConnectionSession>>> clients;

  {
    shared_lock<shared_mutex> lock(stateMutex_);
    auto it = dataNodeReplicationMap_.find(filePath);
    if (it != dataNodeReplicationMap_.end())
      for (auto &entry : it->second)
        datanodes.push_back(entry);
    for (auto &entry : clientEndpointMap_)
      clients.push_back(entry);
  }

  vector<future<Message>> futures;
  futures.reserve(datanodes.size());
  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;
    futures.push_back(requestPool_.submit(
        [session = datanode.second, filePath]() {
          return session->deleteFile(filePath);
        }));
  }
  for (auto &f : futures) {
    Message result = f.get();
    if (!result.isAck())
      cerr << "[ReplicationManager]: datanode failed to delete " << filePath << "\n";
  }

  for (auto &client : clients) {
    if (!client.second || client.first == originProcessName)
      continue;
    client.second->pushDeleteFile(filePath);
  }

  lockManager.eraseLock(filePath);

  {
    unique_lock<shared_mutex> lock(stateMutex_);
    dataNodeReplicationMap_.erase(filePath);
    persistedVersionMap_.erase(filePath);
  }

  fileManager_.deleteFileAndVersion(filePath);
  persistState();
}

// ── Replication map maintenance ───────────────────────────────────────────────

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

  // Persist updated replication map immediately after removing dead node.
  persistState();

  for (auto &target : rebalanceTargets)
    rebalanceFileReplication(target.first, target.second);
}

void ReplicationManager::rebalanceFileReplication(
    const string &filePath,
    map<string, shared_ptr<ConnectionSession>> fileHoldersMap) {
  if (fileHoldersMap.empty())
    return;

  auto sourceSession = fileHoldersMap.begin()->second;
  if (!sourceSession)
    return;

  vector<uint8_t> fileData;
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
    auto &stored = dataNodeReplicationMap_[filePath];
    for (auto &entry : fileHoldersMap)
      stored[entry.first] = entry.second;
  }

  // Persist after rebalance so new holder set is durable.
  persistState();
}

// ── File version map ──────────────────────────────────────────────────────────

map<string, int> ReplicationManager::getFileVersionMap() {
  // Query all live datanodes and reconcile with the persisted map
  // (highest-version-wins as per design).
  map<string, int> fileVersionMap;

  // Seed with persisted state first so offline files are still visible.
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    fileVersionMap = persistedVersionMap_;
  }

  vector<shared_ptr<ConnectionSession>> datanodes;
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    for (auto &entry : dataNodeEndpointMap_)
      datanodes.push_back(entry.second);
  }

  for (auto &datanode : datanodes) {
    if (!datanode || !datanode->isAlive())
      continue;
    Message mex = datanode->syncStatus();
    map<string, int> datanodeMap = mex.getFileVersionMap();
    for (auto &[path, ver] : datanodeMap) {
      auto it = fileVersionMap.find(path);
      if (it == fileVersionMap.end())
        fileVersionMap[path] = ver;
      else
        it->second = max(it->second, ver);
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
