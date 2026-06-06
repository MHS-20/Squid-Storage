#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "peer.hpp"

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 12345

class Client : public Peer {
public:
  using PushHandler =
      std::function<void(const Message &, const std::vector<uint8_t> &)>;

  Client(const std::string &serverIp, int serverPort,
         const std::string &processName);
  ~Client() override;

  void run() override;
  void setPushHandler(PushHandler handler);

  Message createFile(const std::string &filePath,
                     const std::vector<uint8_t> &data, int version = 0);
  Message readFile(const std::string &filePath, std::vector<uint8_t> &dataOut);
  Message updateFile(const std::string &filePath,
                     const std::vector<uint8_t> &data, int version = 0);
  Message deleteFile(const std::string &filePath);
  Message acquireLock(const std::string &filePath);
  Message releaseLock(const std::string &filePath);
  Message syncStatus();
  Message heartbeat();

  int getFileVersion(const std::string &path) const;
  void setFileVersion(const std::string &path, int version);
  void deleteFileVersion(const std::string &path);
  std::map<std::string, int> getVersionMap() const;

protected:
  ConnectionSession::RequestHandler makeRequestHandler() override;

private:
  PushHandler pushHandler_;

  mutable std::mutex versionMutex_;
  std::map<std::string, int> versions_; // in-memory only, no disk I/O

  void setVersionFromAck(const std::string &path, const Message &ack);
  void handlePush(ConnectionSession &session, const Message &message);
};
