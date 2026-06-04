#include "datanode.hpp"
#include <chrono>
#include <thread>

DataNode::DataNode() : DataNode(SERVER_IP, SERVER_PORT) {}
DataNode::DataNode(int port) : Peer(SERVER_IP, port, "DATANODE", "DATANODE") {}
DataNode::DataNode(const char *server_ip, int port)
    : Peer(server_ip, port, "DATANODE", "DATANODE") {}
DataNode::DataNode(std::string nodeType, std::string processName)
    : Peer(std::move(nodeType), std::move(processName)) {}
DataNode::DataNode(int port, std::string nodeType, std::string processName)
    : Peer(port, std::move(nodeType), std::move(processName)) {}
DataNode::DataNode(const char *server_ip, int port, std::string nodeType,
                   std::string processName)
    : Peer(server_ip, port, std::move(nodeType), std::move(processName)) {}

ConnectionSession::RequestHandler DataNode::makeRequestHandler() {
  return [](ConnectionSession &session, const Message &msg) {
    session.responseDispatcher(msg);
  };
}

void DataNode::run() {
  connectToServer();

  while (true) {
    if (!isAlive()) {
      std::cout << "[DATANODE]: Connection lost. Retrying...\n";
      std::this_thread::sleep_for(std::chrono::seconds(3));
      try { reconnect(); } catch (...) { continue; }
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}