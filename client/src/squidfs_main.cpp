#include "ClusterConfig.hpp"
#include "client.hpp"
#include "squidfs.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0]
              << " <mountpoint> <server_ip|cluster.conf>"
                 " [port|processName [processName]]\n";
    return 1;
  }

  const std::string mountpoint = argv[1];
  const std::string arg2 = argv[2];

  std::string serverIp = DEFAULT_SERVER_IP;
  int serverPort = DEFAULT_SERVER_PORT;
  std::string processName = "FUSE_CLIENT";

  const bool isConfigFile =
      arg2.size() > 5 && arg2.substr(arg2.size() - 5) == ".conf";

  if (isConfigFile) {
    // Config-file form: mountpoint  cluster.conf  [processName]
    try {
      ClusterConfig cfg = ClusterConfig::fromFile(arg2);
      if (!cfg.servers.empty()) {
        serverIp = cfg.servers[0].ip;
        serverPort = cfg.servers[0].port;
      }
    } catch (const std::exception &ex) {
      std::cerr << "[squidfs_main]: failed to load config '" << arg2
                << "': " << ex.what() << "\n";
      return 1;
    }
    if (argc > 3)
      processName = argv[3];
  } else {
    // Raw form: mountpoint  server_ip  [port [processName]]
    serverIp = arg2;
    if (argc > 3)
      serverPort = std::atoi(argv[3]);
    if (argc > 4)
      processName = argv[4];
  }

  std::cout << "[squidfs_main]: connecting to " << serverIp << ":" << serverPort
            << " as " << processName << "\n";

  Client client(serverIp, serverPort, processName);
  client.connectToServer();

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  client.syncStatus();

  SquidFS fs(mountpoint, client);

  int rc = fs.run();
  std::cout << "[squidfs_main]: unmounted, rc=" << rc << "\n";

  client.disconnect();
  return rc;
}
