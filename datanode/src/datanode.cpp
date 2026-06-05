#include "datanode.hpp"
#include <chrono>
#include <iostream>
#include <thread>

std::string DataNode::generateName(const std::string &ip, int port) {
  std::string safe_ip = ip;
  for (char &c : safe_ip)
    if (c == '.')
      c = '_';
  return "DATANODE_" + safe_ip + "_" + std::to_string(port);
}

DataNode::DataNode()
    : Peer(SERVER_IP, SERVER_PORT, "DATANODE",
           generateName(SERVER_IP, SERVER_PORT)) {}

DataNode::DataNode(int port)
    : Peer(SERVER_IP, port, "DATANODE", generateName(SERVER_IP, port)) {}

DataNode::DataNode(const char *server_ip, int port)
    : Peer(server_ip, port, "DATANODE", generateName(server_ip, port)) {}

DataNode::DataNode(std::string nodeType, std::string processName)
    : Peer(std::move(nodeType), std::move(processName)) {}

DataNode::DataNode(int port, std::string nodeType, std::string processName)
    : Peer(port, std::move(nodeType), std::move(processName)) {}

DataNode::DataNode(const char *server_ip, int port, std::string nodeType,
                   std::string processName)
    : Peer(server_ip, port, std::move(nodeType), std::move(processName)) {}

ConnectionSession::RequestHandler DataNode::makeRequestHandler() {
  return [this](ConnectionSession &session, const Message &msg) {
    if (msg.opcode == Opcode::NACK_STALE_EPOCH) {
      uint32_t theirEpoch = msg.getUint32(FieldID::EPOCH, 0);
      if (theirEpoch > lastSeenEpoch_)
        lastSeenEpoch_ = theirEpoch;
      session.setIsAlive(false);
      return;
    }
    session.responseDispatcher(msg);
  };
}

void DataNode::run() {
  // Only connect here if no session was established yet (e.g. via
  // connectWithFailover() in main).  Calling connectToServer() unconditionally
  // would clobber the config-resolved leader session with a hardcoded
  // SERVER_IP:SERVER_PORT connection.
  if (!isAlive())
    connectToServer();

  while (true) {
    if (!isAlive()) {
      std::cout << "[DATANODE " << processName_
                << "]: Connection lost. Retrying...\n";
      std::this_thread::sleep_for(std::chrono::seconds(3));
      try {
        reconnect();
      } catch (const std::exception &e) {
        std::cerr << "[DATANODE " << processName_
                  << "]: Reconnect failed: " << e.what() << "\n";
        continue;
      }
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}
