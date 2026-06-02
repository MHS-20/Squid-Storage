#include "server.hpp"
#include "networking/TCPConnectorChannel.hpp"
#include <algorithm>
#include <utility>
using namespace std;

Server::Server() : Server(DEFAULT_PORT, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port) : Server(port, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port, int replicationFactor) : Server(port, replicationFactor, DEFAULT_TIMEOUT) {}

Server::Server(int port, int replicationFactor, int timeoutSeconds)
{
    this->port = port;
    this->replicationFactor = replicationFactor;
    listener_ = std::make_unique<TCPListenerChannel>(port, 3);

    fileTransfer = FileTransfer();
    fileLockMap = map<string, FileLock>();
    // fileTimeMap = map<string, long long>();
    // loadMapFromFile();

    clientEndpointMap = map<string, pair<SquidProtocol, SquidProtocol>>();
    dataNodeEndpointMap = map<string, SquidProtocol>();
    dataNodeReplicationMap = map<string, map<string, SquidProtocol>>();
    endpointIterator = dataNodeEndpointMap.begin();
}

Server::~Server()
{
    if (listener_)
        listener_->close();
}

void Server::run()
{
    cout << "[SERVER]: Server Starting..." << endl;
    cout << "[SERVER]: Server listening on " << this->port << "...\n";

    while (true)
    {
        if (!listener_)
        {
            cerr << "[SERVER]: Listener not available" << endl;
            return;
        }

        auto accepted = listener_->waitForConnection(1);
        if (accepted)
            std::thread(&Server::handleAccept, this, std::move(*accepted)).detach();
        sendHeartbeats(); // datanodes only
        // saveMapToFile(); // save file time map
        checkFileLockExpiration();

        // printMap(dataNodeEndpointMap, "DataNode Endpoint Map");
        // printMap(dataNodeReplicationMap, "DataNode Replication Map");
    }
}

void Server::checkFileLockExpiration()
{
    lock_guard<recursive_mutex> lock(mapMutex);
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

void Server::sendHeartbeats()
{
    lock_guard<recursive_mutex> lock(mapMutex);
    vector<string> erasable = vector<string>();
    for (auto &datanode : dataNodeEndpointMap)
    {
        cout << "sending hearbeat to:" + datanode.first << endl;
        Message heartbeat = datanode.second.heartbeat();
        if (!heartbeat.isAck())
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
    lock_guard<recursive_mutex> lock(mapMutex);
    for (auto &datanodeName : datanodeNames)
    {
        cout << "[SERVER]: Erasing datanode: " + datanodeName + " from replication map" << endl;
        eraseFromReplicationMap(datanodeName);
    }
}

void Server::eraseFromReplicationMap(string datanodeName)
{
    lock_guard<recursive_mutex> lock(mapMutex);
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
    lock_guard<recursive_mutex> lock(mapMutex);
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
        if (!response.isAck())
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
void Server::handleAccept(AcceptedConnection accepted)
{
    auto primaryChannel = accepted.channel;
    auto peerIp = accepted.peerIp;
    if (!primaryChannel)
        return;

    SquidProtocol primaryProtocol(primaryChannel, "[SERVER_PRIMARY]", "SERVER_PRIMARY");
    Message mex = primaryProtocol.identify();
    string peerProcessName = mex.getString(FieldID::PROCESS_NAME);
    string peerNodeType    = mex.getString(FieldID::NODE_TYPE);
    cout << "[SERVER]: Identity received from peer: " + peerProcessName << endl;

    if (peerNodeType == "DATANODE")
    {
        {
            lock_guard<recursive_mutex> lock(mapMutex);
        dataNodeEndpointMap[peerProcessName] = primaryProtocol;
        printMap(dataNodeEndpointMap, "DataNode Endpoint Map");
        }

        cout << "[SERVER]: Building file map..." << endl;
        buildFileLockMap();
        return;
    }
    else if (peerNodeType != "CLIENT")
    {
        cout << "[SERVER]: Unknown node type\n";
        return;
    }

    primaryProtocol.response(true);
    cout << "[SERVER]: Ack sent to client" << endl;

    cout << "[SERVER]: Connecting to client..." << endl;
    Message connectResponse = primaryProtocol.connectServer();
    int secondaryPort = static_cast<int>(connectResponse.getUint32(FieldID::PORT, 0));
    if (secondaryPort == 0)
    {
        cerr << "[SERVER]: Port not found in connect response" << endl;
        return;
    }
    cout << "[SERVER]: Client port: " << secondaryPort << endl;

    auto secondaryChannel = std::make_shared<TCPConnectorChannel>(peerIp, secondaryPort, 60, 2);
    cout << "[SERVER]: Connected to client..." << endl;
    SquidProtocol secondaryProtocol(secondaryChannel, "[SERVER_SECONDARY]", "SERVER_SECONDARY");
    {
        lock_guard<recursive_mutex> lock(mapMutex);
        clientEndpointMap[peerProcessName] = pair(primaryProtocol, secondaryProtocol);
    }

    handleConnection(clientEndpointMap[peerProcessName].first);
}

void Server::handleConnection(SquidProtocol &clientProtocol)
{
    Message mex;
    while (clientProtocol.isAlive())
    {
        try
        {
            mex = clientProtocol.receiveAndParse();
            if (!clientProtocol.isAlive())
                break;
            cout << "[SERVER]: Received message: " + opcodeToString(mex.opcode) << endl;
        }
        catch (exception &e)
        {
            cerr << "[SERVER]: Error receiving message: " << e.what() << endl;
            break;
        }

        string filePath = mex.getString(FieldID::FILE_PATH);
        int fileVersion = static_cast<int>(mex.getUint32(FieldID::FILE_VERSION, 0));

        switch (mex.opcode)
        {
        case Opcode::CREATE_FILE:
            clientProtocol.requestDispatcher(mex);
            propagateCreateFile(filePath, fileVersion, clientProtocol.getProcessName());
            FileManager::getInstance().deleteFileAndVersion(filePath);
            break;
        case Opcode::READ_FILE:
            if (getFileFromDataNode(filePath, clientProtocol))
                clientProtocol.requestDispatcher(mex);
            else
                clientProtocol.response(false);
            FileManager::getInstance().deleteFileAndVersion(filePath);
            break;
        case Opcode::UPDATE_FILE:
            clientProtocol.requestDispatcher(mex);
            propagateUpdateFile(filePath, fileVersion, clientProtocol.getProcessName());
            FileManager::getInstance().deleteFileAndVersion(filePath);
            break;
        case Opcode::DELETE_FILE:
            clientProtocol.requestDispatcher(mex);
            propagateDeleteFile(filePath, clientProtocol.getProcessName());
            dataNodeReplicationMap.erase(filePath);
            FileManager::getInstance().deleteFileAndVersion(filePath);
            break;
        case Opcode::SYNC_STATUS:
            cout << "SERVER: received sync status request\n";
            clientProtocol.response(getFileVersionMap());
            break;
        case Opcode::ACQUIRE_LOCK:
            cout << "[SERVER]: received acquire lock request for " << filePath << endl;
            clientProtocol.response(this->acquireLock(filePath));
            break;
        case Opcode::RELEASE_LOCK:
            this->releaseLock(filePath);
            clientProtocol.response(true);
            break;
        default:
            clientProtocol.requestDispatcher(mex);
        }

        cout << "[SERVER]: Request dispatched" << endl;
    }
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
    lock_guard<recursive_mutex> lock(mapMutex);
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
    lock_guard<recursive_mutex> lock(mapMutex);
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
    lock_guard<recursive_mutex> lock(mapMutex);
    cout << "[SERVER]: Building file map..." << endl;
    for (auto &datanodeEndpoint : dataNodeEndpointMap)
    {
        cout << "[SERVER]: Building file map from datanode: " + datanodeEndpoint.first << endl;
        Message files = datanodeEndpoint.second.listFiles();
        if (!files.isResponse())
        {
            cout << "[SERVER]: NACK for: " + datanodeEndpoint.first << endl;
            continue;
        }
        map<string, int> fileMap = files.getFileVersionMap();
        for (auto &file : fileMap)
        {
            if (fileLockMap.find(file.first) == fileLockMap.end())
            {
                cout << "raw version -> " << file.second << "\n";
                fileLockMap[file.first] = FileLock(file.first);
                FileManager::getInstance().setFileVersion(file.first, file.second);
            }
            else if (FileManager::getInstance().getFileVersion(file.first) > file.second)
            {
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
            else if (FileManager::getInstance().getFileVersion(file.first) < file.second)
            {
                cout << "updating server file version\n";
                FileManager::getInstance().setFileVersion(file.first, file.second);
            }

            dataNodeReplicationMap[file.first].insert(datanodeEndpoint);
            cout << "[SERVER]: File: " + file.first + " added to datanode: " + datanodeEndpoint.first << endl;
        }
    }
    cout << "[SERVER]: File map built successfully" << endl;
}

bool Server::getFileFromDataNode(string filePath, SquidProtocol clientProtocol)
{
    lock_guard<recursive_mutex> lock(mapMutex);
    cout << "retriving file " + filePath << endl;
    if (dataNodeReplicationMap.find(filePath) == dataNodeReplicationMap.end())
    {
        cout << "[SERVER]: File not found in datanode replication map" << endl;
        return false;
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
        return false;
    }

    Message mex = dataNodeHolderProtocol.readFile(filePath);
    if (!mex.isAck())
    {
        cerr << "Error while retriving file from datanode";
        return false;
    }
    else
    {
        cout << "Retrived file from datanode holder" << endl;
        return true;
    }
}

map<string, int> Server::getFileVersionMap()
{
    lock_guard<recursive_mutex> lock(mapMutex);
    map<string, int> fileVersionMap;
    for (auto &datanode : dataNodeEndpointMap)
    {
        Message mex = datanode.second.listFiles();
        map<string, int> datanodeMap = mex.getFileVersionMap();
        for (auto &file : datanodeMap)
        {
            auto it = fileVersionMap.find(file.first);
            if (it == fileVersionMap.end())
                fileVersionMap[file.first] = file.second;
            else
                it->second = max(it->second, file.second);
        }
    }
    return fileVersionMap;
}

// deprecated
void Server::propagateUpdateFile(string filePath, const string &originProcessName)
{
    lock_guard<recursive_mutex> lock(mapMutex);

    for (auto &client : clientEndpointMap)
    {
        if (client.first != originProcessName)
            client.second.second.updateFile(filePath); // second channel
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.updateFile(filePath);

    // fileTimeMap[filePath] = chrono::system_clock::now().time_since_epoch().count();
}

void Server::propagateUpdateFile(string filePath, int version, const string &originProcessName)
{
    lock_guard<recursive_mutex> lock(mapMutex);

    for (auto &client : clientEndpointMap)
    {
        if (client.first != originProcessName)
            client.second.second.updateFile(filePath, version); // second channel
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.updateFile(filePath, version);
}

void Server::propagateDeleteFile(string filePath, const string &originProcessName)
{
    lock_guard<recursive_mutex> lock(mapMutex);
    for (auto &client : clientEndpointMap)
    {
        if (client.first != originProcessName)
            client.second.second.deleteFile(filePath); // second channel
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.deleteFile(filePath);

    fileLockMap.erase(filePath);
    // fileTimeMap.erase(filePath);
    dataNodeReplicationMap.erase(filePath);
}

// deprecated
void Server::propagateCreateFile(string filePath, const string &originProcessName)
{ // round robin replication
    lock_guard<recursive_mutex> lock(mapMutex);
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
        if (client.first != originProcessName)
            client.second.second.createFile(filePath); // second channel
    }
}

void Server::propagateCreateFile(string filePath, int version, const string &originProcessName)
{ // round robin replication
    lock_guard<recursive_mutex> lock(mapMutex);
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
        if (client.first != originProcessName)
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
