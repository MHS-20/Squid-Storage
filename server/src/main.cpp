#include <iostream>
#include <string>
#include <thread>

#include "ClusterConfig.hpp"
#include "epoch_store.hpp"
#include "filemanager.hpp"
#include "replica_watcher.hpp"
#include "server.hpp"

int main(int argc, char **argv) {
  int port = DEFAULT_PORT;
  int replicationFactor = DEFAULT_REPLICATION_FACTOR;
  std::string configPath;
  std::string myName;

  if (argc > 1)
    port = atoi(argv[1]);
  if (argc > 2)
    replicationFactor = atoi(argv[2]);
  if (argc > 3)
    configPath = argv[3];
  if (argc > 4)
    myName = argv[4];

  if (configPath.empty() || myName.empty()) {
    std::cout << "Starting standalone server on port: " << port
              << ", replication factor: " << replicationFactor << std::endl;
    Server server(port, replicationFactor);
    server.run();
    return 0;
  }

  ClusterConfig config;
  try {
    config = ClusterConfig::fromFile(configPath);
  } catch (const std::exception &e) {
    std::cerr << "Failed to load cluster config: " << e.what() << "\n";
    return 1;
  }

  // In HA mode, replicationFactor comes from cluster config.
  replicationFactor = config.replication_factor;

  FileManager fileManager;
  EpochStore epochStore(fileManager);

  bool isPrimary = config.isPrimary(myName);

  if (isPrimary) {
    uint32_t epoch = epochStore.load();
    std::cout << "Starting as PRIMARY '" << myName << "' on port " << port
              << " (epoch=" << epoch << ")\n";
    Server server(port, replicationFactor, epoch);
    server.run();
  } else {
    std::cout << "Starting as STANDBY '" << myName << "'\n";

    ReplicaWatcher watcher(
        config, myName, fileManager, epochStore, [&](uint32_t newEpoch) {
          std::cout << "[main]: promoting to leader (epoch=" << newEpoch
                    << "), starting Server on port " << port << "\n";
          // Run Server on a dedicated thread so the watcher loop continues
          // and can detect/demote if a higher-epoch leader appears (P0.8).
          std::thread([=]() {
            Server server(port, replicationFactor, newEpoch);
            server.run();
          }).detach();
        });

    watcher.start();

    while (true)
      std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}
