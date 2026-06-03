#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

#include "filelock.hpp"
#include "filemanager.hpp"
#include "filetransfer.hpp"
#include "networking/TCPListenerChannel.hpp"
#include "server_runtime.hpp"
#include "squidprotocol.hpp"

#define DEFAULT_PORT 12345
#define BUFFER_SIZE 1024

#define DEFAULT_TIMEOUT 60
#define DEFAULT_LOCK_INTERVAL 5
#define DEFAULT_REPLICATION_FACTOR 2
using namespace std;

class Server {
public:
  Server(int port);
  Server(int port, int replicationFactor);
  Server(int port, int replicationFactor, int timeoutSeconds);
  Server();
  ~Server();

  void run();
  void handleClientRequest(ConnectionSession &clientSession,
                           const Message &message);
  void buildFileLockMap();
  bool releaseLock(string path);
  bool acquireLock(string path);

  void handleConnection(SquidProtocol &clientProtocol);
  void handleAccept(AcceptedConnection accepted);

  void sendHeartbeats();
  void checkFileLockExpiration();
  void eraseFromReplicationMap(vector<string> datanodeNames);
  void eraseFromReplicationMap(string datanodeName);
  void rebalanceFileReplication(
      string filePath,
      map<string, std::shared_ptr<ConnectionSession>> fileHoldersMap);

  bool getFileFromDataNode(string filePath, std::vector<uint8_t> &fileData);
  void propagateCreateFile(string filePath, const string &originProcessName);
  void propagateCreateFile(string filePath, int version,
                           const string &originProcessName);
  bool propagateCreateFile(string filePath, int version,
                           const string &originProcessName,
                           const std::vector<uint8_t> &fileData);
  void propagateUpdateFile(string filePath, const string &originProcessName);
  void propagateUpdateFile(string filePath, int version,
                           const string &originProcessName);
  bool propagateUpdateFile(string filePath, int version,
                           const string &originProcessName,
                           const std::vector<uint8_t> &fileData);
  void propagateDeleteFile(string filePath, const string &originProcessName);

private:
  int port;

  int replicationFactor;
  std::unique_ptr<TCPListenerChannel> listener_;
  ThreadPool requestPool_;
  std::atomic<bool> running_{false};
  std::thread acceptThread_;
  std::thread heartbeatThread_;
  std::thread lockExpiryThread_;

  string filename = "fileTimeMap";
  FileTransfer fileTransfer;
  FileManager fileManager_;

  mutable std::shared_mutex stateMutex;
  map<string, FileLock> fileLockMap;
  map<string, long long> fileTimeMap;

  map<string, std::shared_ptr<ConnectionSession>> dataNodeEndpointMap;
  map<string, std::shared_ptr<ConnectionSession>> clientEndpointMap;

  map<string, map<string, std::shared_ptr<ConnectionSession>>>
      dataNodeReplicationMap;

  size_t roundRobinCursor = 0;

  map<string, int> getFileVersionMap();
  void printMap(map<string, long long> &map, string name);
  void printMap(map<string, std::shared_ptr<ConnectionSession>> &map,
                string name);
  void printMap(map<string, FileLock> &map, string name);
  void
  printMap(map<string, map<string, std::shared_ptr<ConnectionSession>>> &map,
           string name);

  std::vector<std::string> pickDataNodesLocked(size_t count);
  std::shared_ptr<ConnectionSession>
  getDataNodeSessionLocked(const std::string &name);
  std::shared_ptr<ConnectionSession>
  getClientSessionLocked(const std::string &name);
};
