#include "standby_replica_manager.hpp"
#include "replication_manager.hpp"

#include <iostream>

using namespace std;

StandbyReplicaManager::StandbyReplicaManager(FileManager &fileManager,
                                             ReplicationManager &replicationManager,
                                             ThreadPool &pool,
                                             uint32_t epoch)
    : fileManager_(fileManager),
      replicationManager_(replicationManager),
      pool_(pool),
      epoch_(epoch)
{}

void StandbyReplicaManager::registerStandby(
    const string &name,
    shared_ptr<ConnectionSession> session)
{
    // Build the snapshot on the calling thread (already a pool worker).
    // getFileVersionMap() and buildPersistableRepMap() are both thread-safe.
    auto versionMap = replicationManager_.getFileVersionMap();
    auto repMap     = replicationManager_.getPersistableRepMap();

    // Send the full snapshot before registering so there is no window where
    // the standby is in the map but hasn't received the snap yet.
    session->call([&versionMap, &repMap, ep = epoch_](SquidProtocol &proto) {
        proto.sendStateSnap(versionMap, repMap, ep);
        return 0;
    });

    {
        lock_guard<mutex> lock(mu_);
        standbys_[name] = session;
    }

    cout << "[StandbyReplicaManager]: standby '" << name
         << "' registered (epoch=" << epoch_ << ", "
         << versionMap.size() << " file(s) in snapshot)\n";
}

void StandbyReplicaManager::sendDelta(uint8_t op,
                                      const string &filePath,
                                      int version,
                                      const vector<string> &datanodeNames)
{
    vector<shared_ptr<ConnectionSession>> targets;
    {
        lock_guard<mutex> lock(mu_);
        for (auto &[name, s] : standbys_)
            if (s && s->isAlive())
                targets.push_back(s);
    }

    for (auto &session : targets)
    {
        // post() is fire-and-forget — we don't want to block the commit path
        // on standby network latency.
        session->post([op, filePath, version, datanodeNames, ep = epoch_]
                       (SquidProtocol &proto) {
            proto.sendStateDelta(op, filePath, version, datanodeNames, ep);
        });
    }
}

void StandbyReplicaManager::sendHeartbeats()
{
    vector<pair<string, shared_ptr<ConnectionSession>>> snapshot;
    {
        lock_guard<mutex> lock(mu_);
        for (auto &e : standbys_)
            snapshot.push_back(e);
    }

    vector<string> dead;
    for (auto &[name, session] : snapshot)
    {
        if (!session || !session->isAlive()) {
            dead.push_back(name);
            continue;
        }
        session->post([ep = epoch_](SquidProtocol &proto) {
            proto.sendLeaderHb(ep);
        });
    }

    if (!dead.empty()) {
        lock_guard<mutex> lock(mu_);
        for (auto &name : dead)
            standbys_.erase(name);
    }
}

void StandbyReplicaManager::removeStandby(const string &name)
{
    lock_guard<mutex> lock(mu_);
    standbys_.erase(name);
}
