#include "replica_watcher.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <thread>

using namespace std;
using namespace std::chrono;

// ── Construction / destruction
// ────────────────────────────────────────────────

ReplicaWatcher::ReplicaWatcher(const ClusterConfig &config,
                               const string &myName, FileManager &fileManager,
                               EpochStore &epochStore,
                               PromoteCallback onPromote,
                               DemoteCallback onDemote)
    : config_(config), myName_(myName), fileManager_(fileManager),
      epochStore_(epochStore), onPromote_(std::move(onPromote)),
      onDemote_(std::move(onDemote)) {
  // Resume from persisted epoch in case we previously promoted but crashed.
  observedEpoch_ = epochStore_.load();
}

ReplicaWatcher::~ReplicaWatcher() { stop(); }

void ReplicaWatcher::start() {
  running_ = true;
  watcherThread_ = thread([this]() { watchLoop(); });
}

void ReplicaWatcher::stop() {
  running_ = false;
  if (watcherThread_.joinable())
    watcherThread_.join();
}

// ── Public state accessors
// ────────────────────────────────────────────────────

map<string, int> ReplicaWatcher::getVersionMap() const {
  lock_guard<mutex> lock(stateMu_);
  return versionMap_;
}

map<string, set<string>> ReplicaWatcher::getRepMap() const {
  lock_guard<mutex> lock(stateMu_);
  return repMap_;
}

uint32_t ReplicaWatcher::getObservedEpoch() const {
  lock_guard<mutex> lock(stateMu_);
  return observedEpoch_;
}

// ── Main watcher loop
// ─────────────────────────────────────────────────────────

void ReplicaWatcher::watchLoop() {
  cout << "[ReplicaWatcher:" << myName_
       << "]: started (priority=" << config_.priorityOf(myName_) << ")\n";

  while (running_) {
    // If we are already ACTIVE (promoted in a previous iteration), there is
    // nothing more to do — Server is running. Just idle.
    if (role_.load() == Role::ACTIVE) {
      this_thread::sleep_for(milliseconds(200));
      continue;
    }

    // Find the highest-priority live server to follow.
    // We try servers in priority order, stopping at ourselves.
    string followTarget;
    ServerEntry followEntry;
    for (const auto &s : config_.servers) {
      if (s.name == myName_)
        break; // Don't follow a lower-priority server.
      auto ch = tryConnect(s, 1, 0);
      if (ch) {
        // We managed to connect — this is the primary to follow.
        ch->close();
        followTarget = s.name;
        followEntry = s;
        break;
      }
    }

    if (followTarget.empty()) {
      // Could not reach any higher-priority server. Check if we should
      // promote ourselves.
      if (allHigherPriorityDown()) {
        cout << "[ReplicaWatcher:" << myName_
             << "]: all higher-priority servers unreachable — promoting\n";
        promote();
      } else {
        // Shouldn't normally reach here (allHigherPriorityDown just
        // probed them), but be defensive.
        this_thread::sleep_for(milliseconds(config_.heartbeat_timeout_ms));
      }
      continue;
    }

    // Connect properly and enter the standby receive loop.
    cout << "[ReplicaWatcher:" << myName_ << "]: following primary '"
         << followTarget << "' at " << followEntry.ip << ":" << followEntry.port
         << "\n";

    auto ch = tryConnect(followEntry, config_.reconnect_attempts,
                         config_.reconnect_delay_ms);
    if (!ch) {
      cerr << "[ReplicaWatcher:" << myName_
           << "]: could not connect to primary '" << followTarget
           << "' — retrying\n";
      this_thread::sleep_for(milliseconds(config_.reconnect_delay_ms));
      continue;
    }

    // Perform the STANDBY handshake:
    //   1. Receive IDENTIFY from primary
    //   2. Send our identity with node type STANDBY
    //   3. Receive ACK
    {
      SquidProtocol proto(fileManager_, ch, "STANDBY", myName_);
      Message identify = proto.receiveAndParse();
      if (!proto.isAlive() || identify.opcode != Opcode::IDENTIFY) {
        cerr << "[ReplicaWatcher:" << myName_
             << "]: unexpected frame during handshake (expected IDENTIFY)\n";
        ch->close();
        this_thread::sleep_for(milliseconds(config_.reconnect_delay_ms));
        continue;
      }
      proto.response("STANDBY", myName_);
      Message ack = proto.receiveAndParse();
      if (!proto.isAlive() || !ack.isAck()) {
        cerr << "[ReplicaWatcher:" << myName_
             << "]: handshake ACK not received from primary\n";
        ch->close();
        this_thread::sleep_for(milliseconds(config_.reconnect_delay_ms));
        continue;
      }
      cout << "[ReplicaWatcher:" << myName_
           << "]: handshake complete with primary '" << followTarget << "'\n";
    }
    // The SquidProtocol above was temporary (just for handshake). Now
    // we re-wrap the channel for the long-lived receive loop. This avoids
    // seq-number state leaking across the two uses.

    bool timedOut = receiveLoop(ch, fileManager_, followTarget);
    ch->close();

    if (timedOut) {
      cerr << "[ReplicaWatcher:" << myName_ << "]: primary '" << followTarget
           << "' heartbeat timeout — checking for promotion\n";
      // Brief probe to avoid false promotion on transient packet loss.
      this_thread::sleep_for(milliseconds(config_.promotion_probe_delay_ms));
      if (allHigherPriorityDown())
        promote();
      // else: a different server is up; loop will re-connect to it.
    }
    // If not timed out (graceful close), just reconnect to whoever is now
    // primary (loop restarts).
  }
}

// ── Receive loop (standby mode)

bool ReplicaWatcher::receiveLoop(std::shared_ptr<INetworkChannel> channel,
                                 FileManager &fm, const string &primaryName) {
  // We read frames directly from the channel using a fresh SquidProtocol so
  // the seq-number state is clean for this connection.
  SquidProtocol proto(fm, channel, "STANDBY", myName_);

  // Heartbeat deadline: updated every time we receive a LEADER_HB.
  auto deadline =
      steady_clock::now() + milliseconds(config_.heartbeat_timeout_ms);

  while (running_ && role_.load() == Role::STANDBY) {
    // Poll with a short timeout so we can check the heartbeat deadline.
    // We use select() on the raw socket.
    int fd = channel->getSocket();
    if (fd < 0)
      return true;

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100'000; // 100 ms
    int ready = ::select(fd + 1, &rset, nullptr, nullptr, &tv);

    if (ready < 0)
      return true; // error — treat as disconnect

    if (steady_clock::now() > deadline) {
      cerr << "[ReplicaWatcher:" << myName_
           << "]: heartbeat deadline exceeded from '" << primaryName << "'\n";
      return true; // timed out
    }

    if (ready == 0)
      continue; // timeout, check deadline again

    Message msg = proto.receiveAndParse();
    if (!proto.isAlive())
      return false; // graceful close or parse error

    uint32_t incomingEpoch = msg.getUint32(FieldID::EPOCH, 0);

    // Epoch fencing: if the incoming epoch is lower than what we've seen,
    // the sender is a stale leader. We can't send NACK_STALE_EPOCH from a
    // standby (we have no active server role), but we can log and ignore.
    // When clients/datanodes try to connect to a stale server they will
    // fencing-reject it themselves.
    if (incomingEpoch < observedEpoch_) {
      cerr << "[ReplicaWatcher:" << myName_
           << "]: ignoring frame with stale epoch " << incomingEpoch
           << " (known=" << observedEpoch_ << ")\n";
      continue;
    }

    switch (msg.opcode) {
    case Opcode::STATE_SNAP:
      applySnap(msg);
      deadline =
          steady_clock::now() + milliseconds(config_.heartbeat_timeout_ms);
      break;

    case Opcode::STATE_DELTA:
      applyDelta(msg);
      break;

    case Opcode::LEADER_HB: {
      lock_guard<mutex> lk(stateMu_);
      if (incomingEpoch > observedEpoch_)
        observedEpoch_ = incomingEpoch;
    }
      deadline =
          steady_clock::now() + milliseconds(config_.heartbeat_timeout_ms);
      break;

    case Opcode::CLOSE:
      // Primary is shutting down intentionally.
      proto.response(true);
      return false;

    default:
      cerr << "[ReplicaWatcher:" << myName_ << "]: unexpected opcode "
           << opcodeToString(msg.opcode) << " from primary '" << primaryName
           << "'\n";
      break;
    }
  }

  return false;
}

// ── Apply snapshot / delta

void ReplicaWatcher::applySnap(const Message &msg) {
  uint32_t epoch = msg.getUint32(FieldID::EPOCH, 0);

  map<string, int> newVersionMap;
  map<string, set<string>> newRepMap;

  for (const auto &f : msg.fields) {
    if (f.id != FieldID::SNAP_ENTRY)
      continue;
    string entry(f.value.begin(), f.value.end());
    string path;
    int ver;
    set<string> dns;
    if (SquidProtocolFormatter::parseSnapEntry(entry, path, ver, dns)) {
      newVersionMap[path] = ver;
      newRepMap[path] = dns;
    }
  }

  {
    lock_guard<mutex> lk(stateMu_);
    versionMap_ = move(newVersionMap);
    repMap_ = move(newRepMap);
    if (epoch > observedEpoch_)
      observedEpoch_ = epoch;
  }

  cout << "[ReplicaWatcher:" << myName_ << "]: applied snapshot ("
       << versionMap_.size() << " files, epoch=" << epoch << ")\n";
}

void ReplicaWatcher::applyDelta(const Message &msg) {
  uint32_t epoch = msg.getUint32(FieldID::EPOCH, 0);
  uint8_t op = 0;
  for (const auto &f : msg.fields)
    if (f.id == FieldID::DELTA_OP && !f.value.empty()) {
      op = f.value[0];
      break;
    }

  string filePath = msg.getString(FieldID::FILE_PATH, "");
  int version = static_cast<int>(msg.getUint32(FieldID::FILE_VERSION, 0));

  vector<string> datanodes;
  for (const auto &f : msg.fields)
    if (f.id == FieldID::DATANODE_NAME)
      datanodes.push_back(string(f.value.begin(), f.value.end()));

  {
    lock_guard<mutex> lk(stateMu_);
    if (epoch > observedEpoch_)
      observedEpoch_ = epoch;

    if (op == 1) // DELETE
    {
      versionMap_.erase(filePath);
      repMap_.erase(filePath);
    } else // CREATE / UPDATE
    {
      versionMap_[filePath] = version;
      set<string> dns(datanodes.begin(), datanodes.end());
      repMap_[filePath] = move(dns);
    }
  }
}

// ── Promotion logic

bool ReplicaWatcher::allHigherPriorityDown() const {
  auto higher = config_.higherPriorityThan(myName_);
  if (higher.empty())
    return true;

  // Probe higher-priority servers in parallel to minimize failover latency.
  vector<future<bool>> futures;
  for (const auto &s : higher) {
    futures.push_back(async(launch::async, [&s, this]() {
      for (int attempt = 0; attempt < config_.promotion_probe_attempts;
           ++attempt) {
        auto ch = tryConnect(s, 1, 0);
        if (ch) {
          ch->close();
          return true; // reachable
        }
        this_thread::sleep_for(milliseconds(config_.promotion_probe_delay_ms));
      }
      return false; // unreachable
    }));
  }
  for (auto &f : futures)
    if (f.get())
      return false; // at least one higher-priority server is reachable
  return true;
}

void ReplicaWatcher::promote() {
  uint32_t newEpoch;
  {
    lock_guard<mutex> lk(stateMu_);
    newEpoch = observedEpoch_ + 1;
    observedEpoch_ = newEpoch;
  }

  // Persist before accepting any connections.
  epochStore_.save(newEpoch);

  role_ = Role::ACTIVE;

  cout << "[ReplicaWatcher:" << myName_
       << "]: PROMOTED to leader (epoch=" << newEpoch << ")\n";

  if (onPromote_)
    onPromote_(newEpoch);
}

// ── TCP helpers

shared_ptr<INetworkChannel> ReplicaWatcher::tryConnect(const ServerEntry &entry,
                                                       int attempts,
                                                       int delayMs) const {
  for (int i = 0; i < attempts; ++i) {
    try {
      auto ch =
          make_shared<TCPConnectorChannel>(entry.ip.c_str(), entry.port, 1, 1);
      if (ch->isOpen())
        return ch;
    } catch (...) {
    }

    if (i + 1 < attempts && delayMs > 0)
      this_thread::sleep_for(milliseconds(delayMs));
  }
  return nullptr;
}
