#pragma once
#include "peer.hpp"

#define SERVER_IP "127.0.0.1"

class DataNode : public Peer {
public:
  DataNode();
  DataNode(int port);
  DataNode(const char *server_ip, int port);
  DataNode(std::string nodeType, std::string processName);
  DataNode(int port, std::string nodeType, std::string processName);
  DataNode(const char *server_ip, int port, std::string nodeType,
           std::string processName);

  void run() override;

protected:
  ConnectionSession::RequestHandler makeRequestHandler() override;
};