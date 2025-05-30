#include "server.hpp"
#include <algorithm>
using namespace std;

Server::Server() : Server(DEFAULT_PORT, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port) : Server(port, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port, int replicationFactor) : Server(port, replicationFactor, DEFAULT_TIMEOUT) {}

Server::Server(int port, int replicationFactor, int timeoutSeconds)
{
    this->port = port;
    this->replicationFactor = replicationFactor;
    this->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    cout << "[SERVER]: Initializing..." + server_fd << endl;
    if (server_fd < 0)
    {
        perror("[SERVER]: Socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("[SERVER]: setsockopt failed (SO_REUSEADDR)");
        exit(EXIT_FAILURE);
    }

#ifdef SO_REUSEPORT
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        perror("[SERVER]: setsockopt failed (SO_REUSEPORT)");
        exit(EXIT_FAILURE);
    }
#endif

    struct timeval timeout;
    timeout.tv_sec = timeoutSeconds;
    timeout.tv_usec = 0;

    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
    {
        perror("[SERVER]: setsockopt failed (SO_RCVTIMEO)");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
    {
        perror("[SERVER]: setsockopt failed (SO_SNDTIMEO)");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    fileTransfer = FileTransfer();
    fileLockMap = map<string, FileLock>();
    // fileTimeMap = map<string, long long>();
    // loadMapFromFile();

    primarySocketMap = map<int, SquidProtocol>();
    clientEndpointMap = map<string, pair<SquidProtocol, SquidProtocol>>();
    dataNodeEndpointMap = map<string, SquidProtocol>();
    dataNodeReplicationMap = map<string, map<string, SquidProtocol>>();
    endpointIterator = dataNodeEndpointMap.begin();
}

Server::~Server()
{
    close(server_fd);
}

void Server::run()
{
    cout << "[SERVER]: Server Starting..." << endl;
    if (::bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        return;
    }

    listen(server_fd, 3);
    cout << "[SERVER]: Server listening on " << this->port << "...\n";

    int max_sd = server_fd;

    while (true)
    {
        // wait with select
        fd_set master_set, readfds;
        FD_ZERO(&master_set);
        FD_SET(server_fd, &master_set);

        // add all primary sockets to the set
        for (auto &client : primarySocketMap)
        {
            int sd = client.second.getSocket();
            if (sd > 0)
                FD_SET(sd, &master_set);
            if (sd > max_sd)
                max_sd = sd;
        }

        readfds = master_set;

        struct timeval timeout;
        timeout.tv_sec = 1; // 1 second timeout
        timeout.tv_usec = 0;
        if (select(max_sd + 1, &readfds, NULL, NULL, &timeout) < 0)
        {
            cerr << "[SERVER]: Select failed" << endl;
            checkCloseConnetions(master_set, max_sd);
        }
        else
            for (int i = 0; i <= max_sd; i++)
            {
                if (FD_ISSET(i, &readfds))
                {
                    if (i == server_fd)
                    {
                        // new connection
                        new_socket = accept(server_fd, (struct sockaddr *)&peer_addr, &addrlen);
                        cout << "Accepted connection: " << new_socket << "...\n";
                        max_sd = max(max_sd, new_socket);
                        handleAccept(new_socket, peer_addr);
                    }
                    else
                    {
                        // handle the connection
                        auto client = primarySocketMap.find(i);
                        if (client != primarySocketMap.end())
                            handleConnection(client->second);
                    }
                }
            }
        sendHeartbeats(); // datanodes only
        // saveMapToFile(); // save file time map
        checkFileLockExpiration();

        // printMap(dataNodeEndpointMap, "DataNode Endpoint Map");
        // printMap(dataNodeReplicationMap, "DataNode Replication Map");
    }
}

void Server::checkFileLockExpiration()
{
    auto now = chrono::system_clock::now();
    for (auto it = fileLockMap.begin(); it != fileLockMap.end();)
    {
        if (it->second.isLocked() && it->second.getExpiration() < now)
        {
            cout << "[SERVER]: Lock expired for file: " + it->first << endl;

            string clientHolder = it->second.getClientHolder();
            clientEndpointMap.find(clientHolder)->second.second.releaseLock(it->first);
            it->second.setIsLocked(false);
        }
        else
        {
            ++it;
        }
    }
}

void Server::checkCloseConnetions(fd_set &master_set, int max_sd)
{
    std::cout << "[DEBUG]: Checking file descriptors in fd_set..." << std::endl;
    for (int fd = 0; fd <= max_sd; ++fd)
    {
        if (FD_ISSET(fd, &master_set))
        {
            // Check if the file descriptor is valid
            if (fcntl(fd, F_GETFD) == -1)
            {
                FD_CLR(fd, &master_set);
                primarySocketMap[fd].setIsAlive(false);
                clientEndpointMap.erase(primarySocketMap[fd].getProcessName());
                primarySocketMap.erase(fd);
            }
            else
            {
                cout << "[INFO]: Valid file descriptor: " << fd << std::endl;
            }
        }
    }
}

void Server::sendHeartbeats()
{
    vector<string> erasable = vector<string>();
    for (auto &datanode : dataNodeEndpointMap)
    {
        cout << "sending hearbeat to:" + datanode.first << endl;
        Message heartbeat = datanode.second.heartbeat();
        if (heartbeat.args["ACK"] != "ACK")
        {
            cout << "[SERVER]: Heartbeat failed for datanode: " + datanode.first << endl;
            datanode.second.setIsAlive(false);
            datanode.second.closeConn();
            erasable.push_back(datanode.first);
            // dataNodeEndpointMap.erase(datanode.first);
            // eraseFromReplicationMap(datanode.first);
            cout << "[SERVER]: Datanode removed from replication map: " + datanode.first << endl;
        }
    }

    for (auto &datanode : erasable)
    {
        dataNodeEndpointMap.erase(datanode);
        cout << "[SERVER]: Datanode removed from endpoint map: " + datanode << endl;
    }

    // erase all datanodes that are not alive
    eraseFromReplicationMap(erasable);

    // cout << "[SERVER]: Heartbeat sent to all datanodes" << endl;
}

void Server::eraseFromReplicationMap(vector<string> datanodeNames)
{
    for (auto &datanodeName : datanodeNames)
    {
        cout << "[SERVER]: Erasing datanode: " + datanodeName + " from replication map" << endl;
        eraseFromReplicationMap(datanodeName);
    }
}

void Server::eraseFromReplicationMap(string datanodeName)
{
    for (auto it = dataNodeReplicationMap.begin(); it != dataNodeReplicationMap.end();)
    {
        cout << "[SERVER]: Checking file: " + it->first << endl;
        // printMap(it->second, "Datanode holding the file");
        //  for each file check if the datanode endpoint holds the file
        auto datanodeEndpoint = it->second.find(datanodeName);
        if (datanodeEndpoint != it->second.end())
        {
            it->second.erase(datanodeEndpoint->first); // erase from internal map
            cout << "[SERVER]: Datanode " + datanodeName + " removed from replication map of file: " + it->first << endl;

            // check that internal map is not below threshold
            if (it->second.size() < (replicationFactor / 2) + 1)
            {
                cout << "[SERVER]: Datanodes hodling the file: " + it->first + " are below threshold" << endl;
                rebalanceFileReplication(it->first, it->second);
            }
        }
        else
        {
            cout << "[SERVER]: Datanode: " + datanodeName + " not found for file: " + it->first << endl;
        }
        ++it;
    }
}

void Server::rebalanceFileReplication(string filePath, map<string, SquidProtocol> fileHoldersMap)
{
    cout << "[SERVER]: Rebalancing datanodes for file: " << filePath << endl;

    // retrive file from datanode that already holds the file
    ifstream file(filePath);
    if (file)
    {
        file.close();
    }
    else
    {
        cout << "[SERVER]: File not found on server, retrieving from datanode: " << fileHoldersMap.begin()->first << endl;
        getFileFromDataNode(filePath, fileHoldersMap.begin()->second);
    }

    auto endpointIterator = dataNodeEndpointMap.begin();

    // Assign new datanodes until the replication factor is met
    for (int i = 0; i < dataNodeEndpointMap.size(); i++)
    {
        if (endpointIterator == dataNodeEndpointMap.end())
            endpointIterator = dataNodeEndpointMap.begin();

        const string &datanodeName = endpointIterator->first;

        // Skip datanodes that are already holding the file
        if (fileHoldersMap.find(datanodeName) != fileHoldersMap.end())
        {
            endpointIterator++;
            continue;
        }

        // Assign the file to a new datanode
        fileHoldersMap[datanodeName] = endpointIterator->second;

        // Send the file to the newly assigned datanode
        cout << "[SERVER]: Sending file " << filePath << " to datanode: " << datanodeName << endl;
        Message response = endpointIterator->second.createFile(filePath, FileManager::getInstance().getFileVersion(filePath));
        FileManager::getInstance().deleteFile(filePath);
        endpointIterator++;

        // Check if the file transfer was successful
        if (response.args["ACK"] != "ACK")
        {
            cerr << "[SERVER]: Failed to send file " << filePath << " to datanode: " << datanodeName << endl;
            fileHoldersMap.erase(datanodeName); // Remove the datanode from the map if the transfer failed
        }
        else
        {
            cout << "[SERVER]: File " << filePath << " successfully sent to datanode: " << datanodeName << endl;
            if (fileHoldersMap.size() >= replicationFactor)
            {
                cout << "[SERVER]: Replication factor met for file: " << filePath << endl;
                break; // Stop if the replication factor is met
            }
        }
    }

    // Final check to ensure the replication factor is met
    if (fileHoldersMap.size() < replicationFactor)
    {
        cerr << "[SERVER]: Unable to meet replication factor for file: " << filePath << endl;
    }
    else
    {
        cout << "[SERVER]: Replication factor met for file: " << filePath << endl;
    }
}

// ------------------------------
// --- COMMUNICATION HANDLING ---
// ------------------------------
void Server::handleAccept(int new_socket, sockaddr_in peer_addr)
{
    SquidProtocol primaryProtocol = SquidProtocol(new_socket, "[SERVER_PRIMARY]", "SERVER_PRIMARY");
    Message mex = primaryProtocol.identify();
    cout << "[SERVER]: Identity received from peer: " + mex.args["processName"] << endl;

    if (mex.args["nodeType"] == "DATANODE")
    {
        dataNodeEndpointMap[mex.args["processName"]] = primaryProtocol;
        printMap(dataNodeEndpointMap, "DataNode Endpoint Map");

        cout << "[SERVER]: Building file map..." << endl;
        buildFileLockMap();
        return;
    }
    else if (mex.args["nodeType"] != "CLIENT")
    {
        cout << "[SERVER]: Unknown node type\n";
        return;
    }

    primaryProtocol.response(string("ACK"));
    cout << "[SERVER]: Ack sent to client" << endl;

    cout << "[SERVER]: Connecting to client..." << endl;
    Message connectResponse = primaryProtocol.connectServer();
    if (connectResponse.args.find("port") == connectResponse.args.end())
    {
        cerr << "[SERVER]: Port not found in connect response" << endl;
        return;
    }
    std::string portStr = connectResponse.args["port"];
    if (portStr.empty() || !std::all_of(portStr.begin(), portStr.end(), ::isdigit))
    {
        cerr << "[SERVER]: Error: Invalid port value received: " << portStr << endl;
        return;
    }
    int secondaryPort = stoi(portStr);
    cout << "[SERVER]: Client port: " << secondaryPort << endl;

    // Create second connection
    int second_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in second_addr{};
    second_addr.sin_family = AF_INET;
    second_addr.sin_port = htons(secondaryPort);
    second_addr.sin_addr = peer_addr.sin_addr;

    if (connect(second_fd, (struct sockaddr *)&second_addr, sizeof(second_addr)) < 0)
        cerr << "Second connection failed" << endl;
    else
        cout << "[SERVER]: Connected to client..." << endl;

    SquidProtocol secondaryProtocol = SquidProtocol(second_fd, "[SERVER_SECONDARY]", "SERVER_SECONDARY");
    clientEndpointMap[mex.args["processName"]] = pair(primaryProtocol, secondaryProtocol);
    primarySocketMap[new_socket] = primaryProtocol;
    // secondarySocketMap[second_fd] = secondaryProtocol;
}

void Server::handleConnection(SquidProtocol clientProtocol)
{
    Message mex;
    if (clientProtocol.getSocket() < 0)
    {
        cout << "[SERVER]: Closing & Terminating" << endl;
        return;
    }

    try
    {
        mex = clientProtocol.receiveAndParseMessage();
        cout << "[SERVER]: Received message: " + mex.keyword << endl;
    }
    catch (exception &e)
    {
        cerr << "[SERVER]: Error receiving message: " << e.what() << endl;
        // return;
    }

    switch (mex.keyword)
    {
    case CREATE_FILE:
        clientProtocol.requestDispatcher(mex);
        propagateCreateFile(mex.args["filePath"], stoi(mex.args["fileVersion"]), clientProtocol);
        FileManager::getInstance().deleteFileAndVersion(mex.args["filePath"]);
        break;
    case READ_FILE:
        getFileFromDataNode(mex.args["filePath"], clientProtocol);
        clientProtocol.requestDispatcher(mex);
        FileManager::getInstance().deleteFileAndVersion(mex.args["filePath"]);
        break;
    case UPDATE_FILE:
        clientProtocol.requestDispatcher(mex);
        propagateUpdateFile(mex.args["filePath"], stoi(mex.args["fileVersion"]), clientProtocol);
        FileManager::getInstance().deleteFileAndVersion(mex.args["filePath"]);
        break;
    case DELETE_FILE:
        clientProtocol.requestDispatcher(mex);
        propagateDeleteFile(mex.args["filePath"], clientProtocol);
        dataNodeReplicationMap.erase(mex.args["filePath"]);
        FileManager::getInstance().deleteFileAndVersion(mex.args["filePath"]);
        break;
    case SYNC_STATUS:
        cout << "SERVER: received sync status request\n";
        clientProtocol.response(getFileVersionMap());
        break;
    case ACQUIRE_LOCK:
        cout << "[SERVER]: received acquire lock request for " << mex.args["filePath"] << endl;
        clientProtocol.response(this->acquireLock(mex.args["filePath"]));
        break;
    case RELEASE_LOCK:
        this->releaseLock(mex.args["filePath"]);
        clientProtocol.response(string("ACK"));
        break;
    default:
        clientProtocol.requestDispatcher(mex);
    }

    cout << "[SERVER]: Request dispatched" << endl;
    // printMap(fileLockMap, "File Lock Map");
    //  printMap(fileTimeMap, "File Time Map");
    //  printMap(FileManager::getInstance().getFileVersionMap(), "File Version Map");
    // printMap(dataNodeReplicationMap, "DataNode Replication Map");
    // printMap(clientEndpointMap, "Client Endpoint Map");
};

// -----------------------
// ---- FILE LOCKING -----
// -----------------------

bool Server::acquireLock(string path)
{
    if (fileLockMap.find(path) == fileLockMap.end())
    {
        cout << "[SERVER]: File not found in file map... updating file map" << endl;
        buildFileLockMap();
        if (fileLockMap.find(path) == fileLockMap.end())
        {
            cout << "[SERVER]: File not found" << endl;
            return false;
        }
        return false;
    }

    if (!fileLockMap[path].isLocked())
    {
        fileLockMap[path].setIsLocked(true);
        fileLockMap[path].setExpiration(chrono::system_clock::now() + chrono::minutes(DEFAULT_LOCK_INTERVAL));
        return true;
    }
    else
    {
        return false;
    }
}

bool Server::releaseLock(string path)
{
    if (fileLockMap.find(path) == fileLockMap.end())
    {
        cout << "[SERVER]: File not found in file map... updating file map" << endl;
        buildFileLockMap();
        if (fileLockMap.find(path) == fileLockMap.end())
        {
            cout << "[SERVER]: File not found" << endl;
            return false;
        }
        return false;
    }
    else
    {
        fileLockMap[path].setIsLocked(false);
        return true;
    }
}

// -----------------------------
// ---- PROPAGATING EVENTS -----
// -----------------------------

void Server::buildFileLockMap()
{
    cout << "[SERVER]: Building file map..." << endl;
    for (auto &datanodeEndpoint : dataNodeEndpointMap)
    {
        cout << "[SERVER]: Building file map from datanode: " + datanodeEndpoint.first << endl;
        Message files = datanodeEndpoint.second.listFiles(); // <filename; version>
        for (auto &file : files.args)
        {
            if (file.second == "NACK")
            {
                cout << "[SERVER]: NACK for: " + datanodeEndpoint.first << endl;
                // cout << "[SERVER]: File map not built" << endl;
                // return;
                continue;
            }
            if (fileLockMap.find(file.first) == fileLockMap.end())
            { // new file
                cout << "raw version -> " << file.second << "\n";
                fileLockMap[file.first] = FileLock(file.first);
                FileManager::getInstance().setFileVersion(file.first, stoi(file.second));
            }
            else if (FileManager::getInstance().getFileVersion(file.first) > stoi(file.second))
            { // if file on server is neewer, update datanode
                cout << "updating datanode file version\n";
                auto fileHoldersMap = dataNodeReplicationMap[file.first];
                for (auto it = fileHoldersMap.begin(); it != fileHoldersMap.end(); ++it)
                {
                    if (it->first != datanodeEndpoint.first)
                    {
                        cout << "retriving file from datanode: " + it->first << endl;
                        getFileFromDataNode(file.first, it->second);
                        break;
                    }
                }
                datanodeEndpoint.second.updateFile(file.first, FileManager::getInstance().getFileVersion(file.first));
            }
            else if (FileManager::getInstance().getFileVersion(file.first) < stoi(file.second))
            { // if file on server is older, update file time map
                cout << "updating server file version\n";
                FileManager::getInstance().setFileVersion(file.first, stoi(file.second));
            }

            dataNodeReplicationMap[file.first].insert(datanodeEndpoint);
            cout << "[SERVER]: File: " + file.first + " added to datanode: " + datanodeEndpoint.first << endl;
        }
    }
    cout << "[SERVER]: File map built successfully" << endl;
}

void Server::getFileFromDataNode(string filePath, SquidProtocol clientProtocol)
{
    cout << "retriving file " + filePath << endl;
    if (dataNodeReplicationMap.find(filePath) == dataNodeReplicationMap.end())
    {
        cout << "[SERVER]: File not found in datanode replication map" << endl;
        return;
    }

    cout << "file found on datanode" << endl;
    auto &fileHoldersMap = dataNodeReplicationMap[filePath];

    bool check = false;
    SquidProtocol dataNodeHolderProtocol;

    for (auto &datanode : fileHoldersMap)
    {
        if (datanode.second.isAlive())
        {
            dataNodeHolderProtocol = datanode.second;
            check = true;
            break;
        }
    }

    if (!check)
    {
        cerr << "No datanode is alive for: " + filePath;
        return;
    }

    Message mex = dataNodeHolderProtocol.readFile(filePath);
    if (mex.args["ACK"] != "ACK")
        cerr << "Error while retriving file from datanode";
    else
        cout << "Retrived file from datanode holder" << endl;
}

map<string, int> Server::getFileVersionMap()
{
    map<string, int> fileVersionMap;
    for (auto &datanode : dataNodeEndpointMap)
    {

        Message mex = datanode.second.listFiles();

        for (auto &file : mex.args)
        {
            if (fileVersionMap.find(file.first) == fileVersionMap.end())
            {
                fileVersionMap[file.first] = stoi(file.second);
            }
            else
            {
                fileVersionMap[file.first] = max(fileVersionMap[file.first], stoi(file.second));
            }
        }
    }
    return fileVersionMap;
}

// deprecated
void Server::propagateUpdateFile(string filePath, SquidProtocol clientProtocol)
{

    for (auto &client : clientEndpointMap)
    {
        if (client.second.first.getSocket() != clientProtocol.getSocket())
            client.second.second.updateFile(filePath); // second channel
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.updateFile(filePath);

    // fileTimeMap[filePath] = chrono::system_clock::now().time_since_epoch().count();
}

void Server::propagateUpdateFile(string filePath, int version, SquidProtocol clientProtocol)
{

    for (auto &client : clientEndpointMap)
    {
        if (client.second.first.getSocket() != clientProtocol.getSocket())
            client.second.second.updateFile(filePath, version); // second channel
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.updateFile(filePath, version);
}

void Server::propagateDeleteFile(string filePath, SquidProtocol clientProtocol)
{
    for (auto &client : clientEndpointMap)
    {
        if (client.second.first.getSocket() != clientProtocol.getSocket())
            client.second.second.deleteFile(filePath); // second channel
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.deleteFile(filePath);

    fileLockMap.erase(filePath);
    // fileTimeMap.erase(filePath);
    dataNodeReplicationMap.erase(filePath);
}

// deprecated
void Server::propagateCreateFile(string filePath, SquidProtocol clientProtocol)
{ // round robin replication
    lock_guard<mutex> lock(mapMutex);
    auto fileHoldersMap = map<string, SquidProtocol>();

    if (dataNodeEndpointMap.empty())
        return;

    for (int i = 0; i < replicationFactor; i++)
    {
        if (endpointIterator == dataNodeEndpointMap.end())
            endpointIterator = dataNodeEndpointMap.begin();

        fileHoldersMap.insert({endpointIterator->first, endpointIterator->second});
        endpointIterator++;
    }

    cout << "iterated" << endl;
    dataNodeReplicationMap.insert({filePath, fileHoldersMap});

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.createFile(filePath);

    fileLockMap.insert({filePath, FileLock(filePath)});
    // fileTimeMap.insert({filePath, chrono::system_clock::now().time_since_epoch().count()});

    printMap(fileLockMap, "File Lock Map");

    for (auto &client : clientEndpointMap)
    {
        if (client.second.first.getSocket() != clientProtocol.getSocket())
            client.second.second.createFile(filePath); // second channel
    }
}

void Server::propagateCreateFile(string filePath, int version, SquidProtocol clientProtocol)
{ // round robin replication
    lock_guard<mutex> lock(mapMutex);
    auto fileHoldersMap = map<string, SquidProtocol>();

    if (dataNodeEndpointMap.empty())
        return;

    for (int i = 0; i < replicationFactor; i++)
    {
        if (endpointIterator == dataNodeEndpointMap.end())
            endpointIterator = dataNodeEndpointMap.begin();

        fileHoldersMap.insert({endpointIterator->first, endpointIterator->second});
        endpointIterator++;
    }

    cout << "iterated" << endl;
    dataNodeReplicationMap.insert({filePath, fileHoldersMap});

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.createFile(filePath, version);

    fileLockMap.insert({filePath, FileLock(filePath)});
    fileTimeMap.insert({filePath, chrono::system_clock::now().time_since_epoch().count()});
    printMap(fileLockMap, "File Lock Map");

    for (auto &client : clientEndpointMap)
    {
        if (client.second.first.getSocket() != clientProtocol.getSocket())
            client.second.second.createFile(filePath, version); // second channel
    }
}
// -----------------------
// ------ PERSISTANCE ----
// -----------------------
/*
void Server::saveMapToFile()
{
    ofstream outFile(filename);
    for (const auto [key, value] : fileTimeMap)
    {
        outFile << key << ' ' << value << '\n';
    }
    cout << "[SERVER]: File time map saved to file" << endl;
}

void Server::loadMapFromFile()
{
    if (!fs::exists(filename))
    {
        return;
    }

    string key;
    long long value;
    ifstream inFile(filename);

    while (inFile >> key >> value)
    {
        fileTimeMap[key] = value;
    }
    inFile.close();
    cout << "[SERVER]: File time map loaded from file" << endl;
}
*/
// -----------------------
// ------ PRINT MAPS -----
// -----------------------

void Server::printMap(map<string, SquidProtocol> &map, string name)
{
    cout << "[SERVER]: " << name << endl;
    for (auto &pair : map)
    {
        cout << pair.first << " => " << pair.second.toString() << endl;
    }
}

void Server::printMap(map<string, FileLock> &map, string name)
{
    cout << "[SERVER]: " << name << endl;
    for (auto &pair : map)
    {
        cout << pair.first << " => " << pair.second.getFilePath() << " : " << pair.second.isLocked() << endl;
    }
}

void Server::printMap(map<string, map<string, SquidProtocol>> &map, string name)
{
    cout << "[SERVER]: " << name << endl;
    for (auto &pair : map)
    {
        cout << pair.first << " => ";
        for (auto &innerPair : pair.second)
        {
            cout << innerPair.first << " : " << innerPair.second.toString() << ", ";
        }
        cout << endl;
    }
}

void Server::printMap(map<string, long long> &map, string name)
{
    cout << "[SERVER]: " << name << endl;
    for (auto &pair : map)
    {
        cout << pair.first << " => " << pair.second << endl;
    }
}

void Server::printMap(map<string, pair<SquidProtocol, SquidProtocol>> &map, string name)
{
    cout << "[SERVER]: " << name << endl;
    for (auto &pair : map)
    {
        cout << pair.first << " => " << pair.second.first.toString() << " : " << pair.second.second.toString() << endl;
    }
}