#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "ClusterConfig.hpp"
#include "epoch_store.hpp"
#include "filemanager.hpp"
#include "squidprotocol.hpp"
#include "networking/TCPConnectorChannel.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// ReplicaWatcher — runs on a standby server node.
//
// State machine:
//
//   STANDBY  ──────────────────────────────► PROMOTING
//     │  (connected to primary, receiving       │
//     │   snap/deltas and heartbeats)           │ (pre-promotion probe:
//     │                                         │  all higher-priority
//     ▼                                         │  servers unreachable)
//   ACTIVE ◄─────────────────────────────────────
//
//   ACTIVE (this node is now the leader, running Server::run())
//
// The watcher runs its own background thread. When it decides to promote,
// it calls the onPromote callback supplied at construction. That callback
// is responsible for starting the Server's accept loop. The caller (main.cpp
// on a standby) should supply this callback.
//
// Epoch fencing: if a higher-epoch heartbeat arrives while we believe we are
// ACTIVE, we step back down by calling onDemote.
// ─────────────────────────────────────────────────────────────────────────────

class ReplicaWatcher {
public:
    using PromoteCallback = std::function<void(uint32_t newEpoch)>;
    using DemoteCallback  = std::function<void()>;

    ReplicaWatcher(const ClusterConfig &config,
                   const std::string   &myName,
                   FileManager         &fileManager,
                   EpochStore          &epochStore,
                   PromoteCallback      onPromote,
                   DemoteCallback       onDemote = {});

    ~ReplicaWatcher();

    void start();
    void stop();

    // Access the state we have built up from snaps/deltas so that when this
    // node promotes, the new Server can bootstrap from it.
    std::map<std::string, int>                  getVersionMap()     const;
    std::map<std::string, std::set<std::string>>getRepMap()         const;
    uint32_t                                    getObservedEpoch()  const;

private:
    enum class Role { STANDBY, ACTIVE };

    const ClusterConfig &config_;
    std::string          myName_;
    FileManager         &fileManager_;
    EpochStore          &epochStore_;
    PromoteCallback      onPromote_;
    DemoteCallback       onDemote_;

    std::atomic<bool>    running_{false};
    std::atomic<Role>    role_{Role::STANDBY};

    // Guarded state replicated from primary.
    mutable std::mutex   stateMu_;
    std::map<std::string, int>                   versionMap_;
    std::map<std::string, std::set<std::string>> repMap_;
    uint32_t                                     observedEpoch_ = 0;

    std::thread watcherThread_;

    // Background loop
    void watchLoop();

    // Try to connect to the given server and return the connected SquidProtocol,
    // or nullptr on failure. Tries `attempts` times with `delayMs` between each.
    std::shared_ptr<INetworkChannel> tryConnect(const ServerEntry &entry,
                                                int attempts, int delayMs) const;

    // Run the standby receive loop against a connected primary channel.
    // Returns true if the primary gracefully disconnected, false if it timed out.
    bool receiveLoop(INetworkChannel &channel,
                     FileManager &fm,
                     const std::string &primaryName);

    // Apply a single STATE_SNAP message.
    void applySnap(const Message &msg);

    // Apply a single STATE_DELTA message.
    void applyDelta(const Message &msg);

    // Probe all higher-priority servers. Returns true only if ALL are
    // unreachable (i.e. this node may safely promote).
    bool allHigherPriorityDown() const;

    // Perform the promotion: increment epoch, persist, call onPromote_.
    void promote();
};
