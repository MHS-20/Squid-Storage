#pragma once
#include "peer.hpp"

#define SERVER_IP "127.0.0.1"

class DataNode : public Peer {
public:
  // Default: connects to SERVER_IP:SERVER_PORT with an auto-generated name.
  DataNode();

  // Connects to SERVER_IP on the given port; name auto-generated from ip+port.
  explicit DataNode(int port);

  // Connects to the given server; name auto-generated from server_ip+port.
  DataNode(const char *server_ip, int port);

  // Explicit nodeType/processName — use when you need a specific identity
  // (e.g. testing or multi-role nodes).
  DataNode(std::string nodeType, std::string processName);
  DataNode(int port, std::string nodeType, std::string processName);
  DataNode(const char *server_ip, int port, std::string nodeType,
           std::string processName);

  void run() override;

protected:
  ConnectionSession::RequestHandler makeRequestHandler() override;

private:
  // Generates a unique process name from the server address this node connects
  // to, e.g. "DATANODE_127.0.0.1_12345". Prevents collisions in the server's
  // dataNodeEndpointMap_ when multiple datanodes connect.
  static std::string generateName(const std::string &ip, int port);
};
