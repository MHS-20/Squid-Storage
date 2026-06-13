#include "replication_manager.hpp"
#include "lock_manager.hpp"
#include "standby_replica_manager.hpp"
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

  // Try each holder in order.  The final ACK from READ_FILE now carries
  // FILE_VERSION (added to requestDispatcher), so we can check staleness
  // directly from the response without an extra syncStatus() round-trip.
  for (size_t i = 0; i < holders.size(); ++i) {
    vector<uint8_t> candidate;
    Message mex = holders[i]->readFile(filePath, candidate);
    if (!mex.isAck())
      continue;

    int gotVersion = static_cast<int>(mex.getUint32(FieldID::FILE_VERSION, 0));

    if (expectedVersion < 0 || gotVersion >= expectedVersion) {
      // This holder is current (or we have no baseline) — use it.
      fileData = move(candidate);
      return true;
    }

    // Stale holder.  If there are more holders to try, do so.
    if (i + 1 < holders.size()) {
      cerr << "[ReplicationManager]: holder " << i << " is stale for "
           << filePath << " (v" << gotVersion << " < expected v"
           << expectedVersion << ") — trying next holder\n";
      continue;
    }

    // All holders tried and all are stale — version divergence.
    cerr << "[ReplicationManager]: read quorum failed for " << filePath
         << " — no holder has version >= " << expectedVersion << "\n";
    return false;
  }

  return false;
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

  // Fan out to all live datanodes — submit and collect immediately to
  // avoid future-index misalignment if isAlive() changes between passes.
  vector<pair<string, shared_ptr<ConnectionSession>>> ackedNodes;
  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;
    Message response = datanode.second->call([&](SquidProtocol &proto) {
      return proto.createFile(filePath, version, fileData);
    });
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

  // Fanout delta to standbys (fire-and-forget, does not block the commit path).
  if (auto *s = standby_.load(std::memory_order_acquire)) {
    vector<string> holders;
    for (auto &[name, _] : ackedNodes) holders.push_back(name);
    s->sendDelta(0, filePath, version, holders);
  }

  // Push to all other clients.
  for (auto &client : clients) {
    if (!client.second || client.first == originProcessName)
      continue;
    client.second->pushCreateFile(filePath, version, fileData);
  }

  return true;
}

int ReplicationManager::propagateUpdateFile(const string &filePath,
                                            int clientSeenVersion,
                                            const string &originProcessName,
                                            const vector<uint8_t> &fileData) {
  // The server is the version authority: new version = persisted version + 1.
  // We ignore clientSeenVersion for computing the new version (it was already
  // validated against the lock at the server layer), but we keep it as a
  // parameter so the server can log or reject stale writes in the future.
  int newVersion = -1;
  {
    shared_lock<shared_mutex> lock(stateMutex_);
    auto it = persistedVersionMap_.find(filePath);
    newVersion = (it != persistedVersionMap_.end() ? it->second : clientSeenVersion) + 1;
  }

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

  // Fan out in parallel — submit and collect immediately to avoid
  // future-index misalignment if isAlive() changes between passes.
  vector<pair<string, shared_ptr<ConnectionSession>>> ackedNodes;
  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;
    Message response = datanode.second->call([&](SquidProtocol &proto) {
      return proto.updateFile(filePath, newVersion, fileData);
    });
    if (response.isAck())
      ackedNodes.push_back(datanode);
    else
      cerr << "[ReplicationManager]: UPDATE_FILE NACK from " << datanode.first << "\n";
  }

  // Write-quorum check.
  size_t effectiveQuorum = min(quorum_, datanodes.size());
  if (ackedNodes.size() < effectiveQuorum) {
    cerr << "[ReplicationManager]: write quorum not met for UPDATE "
         << filePath << " (" << ackedNodes.size() << "/" << effectiveQuorum
         << " ACKs) — rejecting\n";
    return -1;
  }

  // Quorum met — update persisted state.
  {
    unique_lock<shared_mutex> lock(stateMutex_);
    persistedVersionMap_[filePath] = newVersion;
  }

  fileManager_.setFileVersion(filePath, newVersion);
  persistState();

  // Fanout delta to standbys.
  if (auto *s = standby_.load(std::memory_order_acquire)) {
    vector<string> holders;
    {
      shared_lock<shared_mutex> lock(stateMutex_);
      auto it = dataNodeReplicationMap_.find(filePath);
      if (it != dataNodeReplicationMap_.end())
        for (auto &[name, _] : it->second) holders.push_back(name);
    }
    s->sendDelta(0, filePath, newVersion, holders);
  }

  // Push new version to all other clients.
  for (auto &client : clients) {
    if (!client.second || client.first == originProcessName)
      continue;
    client.second->pushUpdateFile(filePath, newVersion, fileData);
  }

  return newVersion;
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

  for (auto &datanode : datanodes) {
    if (!datanode.second || !datanode.second->isAlive())
      continue;
    Message result = datanode.second->call(
        [&](SquidProtocol &proto) { return proto.deleteFile(filePath); });
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

  // Fanout delete delta to standbys.
  if (auto *s = standby_.load(std::memory_order_acquire))
    s->sendDelta(1, filePath, 0, {});
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

int ReplicationManager::getKnownVersion(const string &filePath) {
  shared_lock<shared_mutex> lock(stateMutex_);
  auto it = persistedVersionMap_.find(filePath);
  return (it != persistedVersionMap_.end()) ? it->second : -1;
}

map<string, int> ReplicationManager::getFileVersionMap() {  // Query all live datanodes and reconcile with the persisted map
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

map<string, set<string>> ReplicationManager::getPersistableRepMap() {
  shared_lock<shared_mutex> lock(stateMutex_);
  return buildPersistableRepMap();
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
