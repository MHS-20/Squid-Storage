#include "ClusterConfig.hpp"
#include "datanode.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

int main(int argc, char **argv) {
  const char *server_ip = SERVER_IP;
  int server_port = SERVER_PORT;
  std::string node_identity;
  std::string configPath;

  if (argc > 1)
    server_ip = argv[1];
  if (argc > 2)
    server_port = atoi(argv[2]);
  if (argc > 3)
    node_identity = argv[3];
  if (argc > 4)
    configPath = argv[4];

  if (node_identity.empty())
    node_identity = fs::current_path().filename().string();

  std::cout << "Starting DataNode [" << node_identity
            << "] -> Connecting to Server: " << server_ip << ":" << server_port
            << std::endl;

  DataNode datanode(server_ip, server_port, std::string("DATANODE"),
                    node_identity);

  if (!configPath.empty()) {
    try {
      auto config = ClusterConfig::fromFile(configPath);
      // connectWithFailover establishes the session against the current leader
      // and stores the config internally so reconnect() can use failover too.
      // DataNode::run() checks isAlive() before calling connectToServer(), so
      // this session will not be clobbered by the run loop.
      datanode.connectWithFailover(config);
    } catch (const std::exception &e) {
      std::cerr << "Failover connect failed: " << e.what()
                << " — falling back to single server\n";
      // run() will call connectToServer() because isAlive() is false.
    }
  }

  datanode.run();
  return 0;
}
