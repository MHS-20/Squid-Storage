#include "datanode.hpp"
#include <chrono>
#include <iostream>
#include <thread>

// ── Name generation ───────────────────────────────────────────────────────────

std::string DataNode::generateName(const std::string &ip, int port) {
  // Replace dots with underscores so the name is a valid map key without
  // any ambiguity when printed or logged.
  std::string safe_ip = ip;
  for (char &c : safe_ip)
    if (c == '.') c = '_';
  return "DATANODE_" + safe_ip + "_" + std::to_string(port);
}

// ── Constructors ──────────────────────────────────────────────────────────────

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

// ── Request handler ───────────────────────────────────────────────────────────

ConnectionSession::RequestHandler DataNode::makeRequestHandler() {
  // DataNodes are purely passive storage. All incoming frames are requests
  // from the server (CREATE, READ, UPDATE, DELETE, SYNC_STATUS, HEARTBEAT,
  // CLOSE). responseDispatcher delegates to requestDispatcher for all of these.
  // PUSH_* opcodes are logged and ignored — the server never pushes to datanodes.
  return [](ConnectionSession &session, const Message &msg) {
    session.responseDispatcher(msg);
  };
}

// ── Run loop ──────────────────────────────────────────────────────────────────

void DataNode::run() {
  connectToServer();

  while (true) {
    if (!isAlive()) {
      std::cout << "[DATANODE " << processName_ << "]: Connection lost. Retrying...\n";
      std::this_thread::sleep_for(std::chrono::seconds(3));
      try {
        reconnect();
      } catch (const std::exception &e) {
        std::cerr << "[DATANODE " << processName_ << "]: Reconnect failed: "
                  << e.what() << "\n";
        continue;
      }
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}
