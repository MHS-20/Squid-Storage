#pragma once
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "ConnectionSession.hpp"
#include "filesystem/filemanager.hpp"
#include "networking/TCPConnectorChannel.hpp"
#include "filelock.hpp"
#include "filetransfer.hpp"
#include "squidprotocol.hpp"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345

class Peer {
public:
  Peer();
  Peer(std::string nodeType, std::string processName);
  Peer(int port, std::string nodeType, std::string processName);
  Peer(std::string server_ip, int port, std::string nodeType,
       std::string processName);
  virtual ~Peer();

  virtual void connectToServer();
  virtual void reconnect();
  virtual void disconnect();
  virtual bool isAlive() const;

  virtual void run() = 0;

protected:
  int port = SERVER_PORT;
  std::string server_ip = SERVER_IP;
  int timeoutSeconds = 60;

  std::string nodeType_;
  std::string processName_;

  FileManager fileManager_;
  FileTransfer fileTransfer_;

  std::shared_ptr<ConnectionSession> session_;
  virtual ConnectionSession::RequestHandler makeRequestHandler() = 0;
  virtual void onConnected() {}
};