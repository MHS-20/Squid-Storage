#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "ConnectionSession.hpp"
#include "filemanager.hpp"
#include "squidprotocol.hpp"
#include "thread_pool.hpp"

class ReplicationManager;

// ─────────────────────────────────────────────────────────────────────────────
// StandbyReplicaManager — primary-side manager for STANDBY connections.
//
// Responsibilities:
//   1. Accept a STANDBY session handed off from Server::handleAccept.
//   2. Send a full STATE_SNAP on first connect so the standby has the
//      complete version map and replication map.
//   3. After every committed write, fanout a STATE_DELTA to all connected
//      standbys.
//   4. Periodically send LEADER_HB frames so standbys can detect primary death.
//
// Thread-safety: all public methods are thread-safe.
// ─────────────────────────────────────────────────────────────────────────────
class StandbyReplicaManager {
public:
    StandbyReplicaManager(FileManager &fileManager,
                          ReplicationManager &replicationManager,
                          ThreadPool &pool,
                          uint32_t epoch);

    // Called by Server::handleAccept when a STANDBY connects.
    // Sends the full snapshot immediately, then registers the session for
    // future delta/heartbeat fanout.
    void registerStandby(const std::string &name,
                         std::shared_ptr<ConnectionSession> session);

    // Called by ReplicationManager after every committed write.
    // op: 0=CREATE/UPDATE, 1=DELETE
    void sendDelta(uint8_t op,
                   const std::string &filePath,
                   int version,
                   const std::vector<std::string> &datanodeNames);

    // Called by Server's heartbeat thread.
    void sendHeartbeats();

    // Remove a standby (e.g. it disconnected).
    void removeStandby(const std::string &name);

    uint32_t epoch() const { return epoch_; }

private:
    FileManager         &fileManager_;
    ReplicationManager  &replicationManager_;
    ThreadPool          &pool_;
    uint32_t             epoch_;

    mutable std::mutex   mu_;
    std::map<std::string, std::shared_ptr<ConnectionSession>> standbys_;
};
