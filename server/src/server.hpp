#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <memory>
#include <thread>
#include <unistd.h>
#include <vector>

#include "filelock.hpp"
#include "filemanager.hpp"
#include "filetransfer.hpp"
#include "networking/TCPListenerChannel.hpp"
#include "squidprotocol.hpp"

#define DEFAULT_PORT 12345
#define BUFFER_SIZE 1024

#define DEFAULT_TIMEOUT 60      // seconds
#define DEFAULT_LOCK_INTERVAL 5 // minutes
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
  void buildFileLockMap();
  bool releaseLock(string path);
  bool acquireLock(string path);

  void handleConnection(SquidProtocol &clientProtocol);
  void handleAccept(AcceptedConnection accepted);

  void sendHeartbeats();
  void checkFileLockExpiration();
  void eraseFromReplicationMap(vector<string> datanodeNames);
  void eraseFromReplicationMap(string datanodeName);
  void rebalanceFileReplication(string filePath,
                                map<string, SquidProtocol> fileHoldersMap);

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

  string filename = "fileTimeMap";
  FileTransfer fileTransfer;

  recursive_mutex mapMutex;
  map<string, FileLock> fileLockMap;
  map<string, long long> fileTimeMap;

  map<string, SquidProtocol> dataNodeEndpointMap;
  map<string, pair<SquidProtocol, SquidProtocol>> clientEndpointMap;

  // maps filename to datanode holding that file (datanode, socket)
  map<string, map<string, SquidProtocol>> dataNodeReplicationMap;

  // iterators for round robin redundancy
  map<string, SquidProtocol>::iterator endpointIterator;

  map<string, int> getFileVersionMap();
  void printMap(map<string, long long> &map, string name);
  void printMap(map<string, SquidProtocol> &map, string name);
  void printMap(map<string, FileLock> &map, string name);
  void printMap(map<string, map<string, SquidProtocol>> &map, string name);
  void printMap(map<string, pair<SquidProtocol, SquidProtocol>> &map,
                string name);
};
