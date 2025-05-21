#include <vector>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fstream>
#include <map>
#include <thread>
#include <mutex>
#include <fcntl.h>
#include <cerrno>

#include "filelock.hpp"
#include "filemanager.hpp"
#include "filetransfer.hpp"
#include "squidProtocolServer.cpp"

#define DEFAULT_PORT 12345
#define BUFFER_SIZE 1024
#define DEFAULT_PATH "./test_txt/test_server"

#define DEFAULT_TIMEOUT 60 // seconds
#define DEFAULT_LOCK_INTERVAL 5 // minutes
#define DEFAULT_REPLICATION_FACTOR 2
using namespace std;

class Server
{
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

    void handleConnection(SquidProtocol clientProtocol);
    void handleAccept(int new_socket, sockaddr_in peer_addr);

    void sendHeartbeats();
    void checkFileLockExpiration();
    void eraseFromReplicationMap(vector<string> datanodeNames);
    void eraseFromReplicationMap(string datanodeName);
    void checkCloseConnetions(fd_set &master_set, int max_sd);
    void rebalanceFileReplication(string filePath, map<string, SquidProtocol> fileHoldersMap);

    void getFileFromDataNode(string filePath, SquidProtocol clientProtocol);
    void propagateCreateFile(string filePath, SquidProtocol clientProtocol);
    void propagateCreateFile(string filePath, int version, SquidProtocol clientProtocol);
    void propagateUpdateFile(string filePath, SquidProtocol clientProtocol);
    void propagateUpdateFile(string filePath, int version, SquidProtocol clientProtocol);
    void propagateDeleteFile(string filePath, SquidProtocol clientProtocol);

private:
    int port;
    int opt = 1;
    char buffer[BUFFER_SIZE] = {0};

    int replicationFactor;
    int server_fd, new_socket;
    struct sockaddr_in address, peer_addr;
    socklen_t addrlen = sizeof(address);
    
    string filename = "fileTimeMap";
    FileTransfer fileTransfer;

    mutex mapMutex;
    map<string, FileLock> fileLockMap;
    map<string, long long> fileTimeMap;

    map<string, SquidProtocol> dataNodeEndpointMap;
    map<string, pair<SquidProtocol, SquidProtocol>> clientEndpointMap;

    map<int, SquidProtocol> primarySocketMap;
    // map<int, SquidProtocol> secondarySocketMap;

    // maps filename to datanode holding that file (datanode, socket)
    map<string, map<string, SquidProtocol>> dataNodeReplicationMap;

    // iterators for round robin redundancy
    map<string, SquidProtocol>::iterator endpointIterator;


    map<string, int> getFileVersionMap();
    void printMap(map<string, long long> &map, string name);
    void printMap(map<string, SquidProtocol> &map, string name);
    void printMap(map<string, FileLock> &map, string name);
    void printMap(map<string, map<string, SquidProtocol>> &map, string name);
    void printMap(map<string, pair<SquidProtocol, SquidProtocol>> &map, string name);
};
