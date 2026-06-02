#include "server.hpp"
#include "networking/TCPConnectorChannel.hpp"

#include <algorithm>
#include <chrono>
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
    (void)timeoutSeconds;

    fileTransfer = FileTransfer();
    fileLockMap = map<string, FileLock>();
    fileTimeMap = map<string, long long>();
    clientEndpointMap = map<string, shared_ptr<ConnectionSession>>();
    dataNodeEndpointMap = map<string, shared_ptr<ConnectionSession>>();
    dataNodeReplicationMap = map<string, map<string, shared_ptr<ConnectionSession>>>();
}

Server::~Server()
{
    running_ = false;
    if (listener_)
        listener_->close();

    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        for (auto &entry : dataNodeEndpointMap)
        {
            if (entry.second)
                entry.second->stop();
        }

        for (auto &entry : clientEndpointMap)
        {
            if (entry.second)
                entry.second->stop();
        }
    }

    if (acceptThread_.joinable())
        acceptThread_.join();
    if (heartbeatThread_.joinable())
        heartbeatThread_.join();
    if (lockExpiryThread_.joinable())
        lockExpiryThread_.join();
}

void Server::run()
{
    cout << "[SERVER]: Server Starting..." << endl;
    cout << "[SERVER]: Server listening on " << this->port << "...\n";

    running_ = true;

    acceptThread_ = std::thread([this]() {
        while (running_)
        {
            if (!listener_)
            {
                cerr << "[SERVER]: Listener not available" << endl;
                running_ = false;
                return;
            }

            auto accepted = listener_->waitForConnection(1);
            if (accepted)
                handleAccept(std::move(*accepted));
        }
    });

    heartbeatThread_ = std::thread([this]() {
        while (running_)
        {
            sendHeartbeats();
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });

    lockExpiryThread_ = std::thread([this]() {
        while (running_)
        {
            checkFileLockExpiration();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    if (acceptThread_.joinable())
        acceptThread_.join();

    running_ = false;

    if (heartbeatThread_.joinable())
        heartbeatThread_.join();
    if (lockExpiryThread_.joinable())
        lockExpiryThread_.join();
}

void Server::handleClientRequest(ConnectionSession &clientSession, const Message &mex)
{
    string filePath = mex.getString(FieldID::FILE_PATH);
    int fileVersion = static_cast<int>(mex.getUint32(FieldID::FILE_VERSION, 0));

    switch (mex.opcode)
    {
    case Opcode::CREATE_FILE:
    {
        clientSession.response(true);
        vector<uint8_t> fileData;
        if (!clientSession.receiveFileData(fileData))
        {
            clientSession.response(false);
            break;
        }

        bool ok = requestPool_.submit([this, filePath, fileVersion, origin = clientSession.getProcessName(), fileData]() {
            return propagateCreateFile(filePath, fileVersion, origin, fileData);
        }).get();

        clientSession.response(ok);
        break;
    }
    case Opcode::READ_FILE:
    {
        auto result = requestPool_.submit([this, filePath]() {
            vector<uint8_t> fileData;
            bool ok = getFileFromDataNode(filePath, fileData);
            return std::make_pair(ok, fileData);
        }).get();

        if (result.first)
        {
            clientSession.response(true);
            if (clientSession.sendFileData(result.second))
                clientSession.response(true);
            else
                clientSession.response(false);
        }
        else
        {
            clientSession.response(false);
        }
        break;
    }
    case Opcode::UPDATE_FILE:
    {
        clientSession.response(true);
        vector<uint8_t> fileData;
        if (!clientSession.receiveFileData(fileData))
        {
            clientSession.response(false);
            break;
        }

        bool ok = requestPool_.submit([this, filePath, fileVersion, origin = clientSession.getProcessName(), fileData]() {
            return propagateUpdateFile(filePath, fileVersion, origin, fileData);
        }).get();

        clientSession.response(ok);
        break;
    }
    case Opcode::DELETE_FILE:
    {
        bool ok = requestPool_.submit([this, filePath, origin = clientSession.getProcessName()]() {
            propagateDeleteFile(filePath, origin);
            return true;
        }).get();
        clientSession.response(ok);
        break;
    }
    case Opcode::SYNC_STATUS:
        clientSession.response(requestPool_.submit([this]() { return getFileVersionMap(); }).get());
        break;
    case Opcode::ACQUIRE_LOCK:
        clientSession.response(requestPool_.submit([this, filePath]() { return acquireLock(filePath); }).get());
        break;
    case Opcode::RELEASE_LOCK:
        clientSession.response(requestPool_.submit([this, filePath]() { return releaseLock(filePath); }).get());
        break;
    case Opcode::HEARTBEAT:
        clientSession.response(true);
        break;
    case Opcode::CLOSE:
        clientSession.response(true);
        clientSession.closeConn();
        clientSession.setIsAlive(false);
        break;
    default:
        clientSession.response(false);
        break;
    }
}

void Server::handleAccept(AcceptedConnection accepted)
{
    auto channel = accepted.channel;
    if (!channel)
        return;

    SquidProtocol proto(channel, "[SERVER]", "SERVER");
    Message mex = proto.identify();
    string peerProcessName = mex.getString(FieldID::PROCESS_NAME);
    string peerNodeType = mex.getString(FieldID::NODE_TYPE);
    cout << "[SERVER]: Identity received from peer: " + peerProcessName << endl;

    if (peerNodeType == "DATANODE")
    {
        auto datanodeSession = std::make_shared<ConnectionSession>(channel, "DATANODE", peerProcessName);
        datanodeSession->start(false);

        {
            std::unique_lock<std::shared_mutex> lock(stateMutex);
            dataNodeEndpointMap[peerProcessName] = datanodeSession;
        }

        cout << "[SERVER]: Building file map..." << endl;
        buildFileLockMap();
        return;
    }

    if (peerNodeType != "CLIENT")
    {
        cout << "[SERVER]: Unknown node type\n";
        return;
    }

    proto.response(true);
    cout << "[SERVER]: Ack sent to client" << endl;

    auto clientSession = std::make_shared<ConnectionSession>(channel, "CLIENT", peerProcessName,
        [this](ConnectionSession &session, const Message &message) {
            handleClientRequest(session, message);
        });

    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        clientEndpointMap[peerProcessName] = clientSession;
    }

    clientSession->start(true);
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
        {
            clientProtocol.response(true);
            vector<uint8_t> fileData;
            if (clientProtocol.receiveFileData(fileData))
            {
                bool ok = propagateCreateFile(filePath, fileVersion, clientProtocol.getProcessName(), fileData);
                FileManager::getInstance().setFileVersion(filePath, fileVersion);
                clientProtocol.response(ok);
            }
            else
            {
                clientProtocol.response(false);
            }
            break;
        }
        case Opcode::READ_FILE:
        {
            vector<uint8_t> fileData;
            if (getFileFromDataNode(filePath, fileData))
            {
                clientProtocol.response(true);
                if (clientProtocol.sendFileData(fileData))
                    clientProtocol.response(true);
                else
                    clientProtocol.response(false);
            }
            else
            {
                clientProtocol.response(false);
            }
            break;
        }
        case Opcode::UPDATE_FILE:
        {
            clientProtocol.response(true);
            vector<uint8_t> fileData;
            if (clientProtocol.receiveFileData(fileData))
            {
                bool ok = propagateUpdateFile(filePath, fileVersion, clientProtocol.getProcessName(), fileData);
                FileManager::getInstance().setFileVersion(filePath, fileVersion);
                clientProtocol.response(ok);
            }
            else
            {
                clientProtocol.response(false);
            }
            break;
        }
        case Opcode::DELETE_FILE:
        {
            propagateDeleteFile(filePath, clientProtocol.getProcessName());
            clientProtocol.response(true);
            break;
        }
        case Opcode::SYNC_STATUS:
            clientProtocol.response(getFileVersionMap());
            break;
        case Opcode::ACQUIRE_LOCK:
            clientProtocol.response(this->acquireLock(filePath));
            break;
        case Opcode::RELEASE_LOCK:
            this->releaseLock(filePath);
            clientProtocol.response(true);
            break;
        default:
            clientProtocol.requestDispatcher(mex);
        }
    }
}

bool Server::acquireLock(string path)
{
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    if (fileLockMap.find(path) == fileLockMap.end())
    {
        lock.unlock();
        buildFileLockMap();
        lock.lock();
        if (fileLockMap.find(path) == fileLockMap.end())
            return false;
        return false;
    }

    if (!fileLockMap[path].isLocked())
    {
        fileLockMap[path].setIsLocked(true);
        fileLockMap[path].setExpiration(chrono::system_clock::now() + chrono::minutes(DEFAULT_LOCK_INTERVAL));
        return true;
    }

    return false;
}

bool Server::releaseLock(string path)
{
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    if (fileLockMap.find(path) == fileLockMap.end())
    {
        lock.unlock();
        buildFileLockMap();
        lock.lock();
        if (fileLockMap.find(path) == fileLockMap.end())
            return false;
        return false;
    }

    fileLockMap[path].setIsLocked(false);
    return true;
}

void Server::buildFileLockMap()
{
    vector<std::shared_ptr<ConnectionSession>> datanodes;
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        for (auto &entry : dataNodeEndpointMap)
            datanodes.push_back(entry.second);
    }

    for (auto &datanodeSession : datanodes)
    {
        if (!datanodeSession || !datanodeSession->isAlive())
            continue;

        Message files = datanodeSession->listFiles();
        if (!files.isResponse())
            continue;

        map<string, int> fileMap = files.getFileVersionMap();
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        for (auto &file : fileMap)
        {
            if (fileLockMap.find(file.first) == fileLockMap.end())
                fileLockMap[file.first] = FileLock(file.first);

            int localVersion = FileManager::getInstance().getFileVersion(file.first);
            if (localVersion < file.second)
                FileManager::getInstance().setFileVersion(file.first, file.second);

            dataNodeReplicationMap[file.first][datanodeSession->getProcessName()] = datanodeSession;
        }
    }
}

bool Server::getFileFromDataNode(string filePath, vector<uint8_t> &fileData)
{
    std::shared_ptr<ConnectionSession> dataNodeHolder;
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        auto it = dataNodeReplicationMap.find(filePath);
        if (it == dataNodeReplicationMap.end())
            return false;

        for (auto &datanode : it->second)
        {
            if (datanode.second && datanode.second->isAlive())
            {
                dataNodeHolder = datanode.second;
                break;
            }
        }
    }

    if (!dataNodeHolder)
        return false;

    Message mex = dataNodeHolder->readFile(filePath, fileData);
    return mex.isAck();
}

map<string, int> Server::getFileVersionMap()
{
    map<string, int> fileVersionMap;
    vector<std::shared_ptr<ConnectionSession>> datanodes;
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        for (auto &entry : dataNodeEndpointMap)
            datanodes.push_back(entry.second);
    }

    for (auto &datanode : datanodes)
    {
        if (!datanode || !datanode->isAlive())
            continue;

        Message mex = datanode->listFiles();
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

void Server::sendHeartbeats()
{
    vector<pair<string, std::shared_ptr<ConnectionSession>>> datanodes;
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        for (auto &entry : dataNodeEndpointMap)
            datanodes.push_back(entry);
    }

    vector<string> deadNodes;
    vector<std::future<Message>> futures;
    futures.reserve(datanodes.size());

    for (auto &entry : datanodes)
    {
        if (!entry.second)
            continue;
        futures.push_back(requestPool_.submit([session = entry.second]() { return session->heartbeat(); }));
    }

    for (size_t i = 0; i < datanodes.size(); ++i)
    {
        auto &entry = datanodes[i];
        if (!entry.second)
            continue;

        Message heartbeat = futures[i].get();
        if (!heartbeat.isAck())
        {
            entry.second->setIsAlive(false);
            deadNodes.push_back(entry.first);
        }
    }

    if (!deadNodes.empty())
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        for (auto &name : deadNodes)
            dataNodeEndpointMap.erase(name);
    }

    eraseFromReplicationMap(deadNodes);
}

void Server::checkFileLockExpiration()
{
    vector<pair<string, string>> expiredLocks;
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        auto now = chrono::system_clock::now();
        for (auto &entry : fileLockMap)
        {
            if (entry.second.isLocked() && entry.second.getExpiration() < now)
                expiredLocks.emplace_back(entry.first, entry.second.getClientHolder());
        }
    }

    for (auto &expired : expiredLocks)
    {
        auto clientSession = getClientSessionLocked(expired.second);
        if (clientSession)
            clientSession->post([expiredFile = expired.first](SquidProtocol &protocol) {
                protocol.releaseLock(expiredFile);
            });

        std::unique_lock<std::shared_mutex> lock(stateMutex);
        auto it = fileLockMap.find(expired.first);
        if (it != fileLockMap.end())
            it->second.setIsLocked(false);
    }
}

void Server::eraseFromReplicationMap(vector<string> datanodeNames)
{
    for (auto &datanodeName : datanodeNames)
        eraseFromReplicationMap(datanodeName);
}

void Server::eraseFromReplicationMap(string datanodeName)
{
    vector<std::pair<string, map<string, std::shared_ptr<ConnectionSession>>>> rebalanceTargets;
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        for (auto it = dataNodeReplicationMap.begin(); it != dataNodeReplicationMap.end(); ++it)
        {
            auto datanodeEndpoint = it->second.find(datanodeName);
            if (datanodeEndpoint == it->second.end())
                continue;

            it->second.erase(datanodeEndpoint->first);
            if (it->second.size() < (replicationFactor / 2) + 1)
                rebalanceTargets.push_back({it->first, it->second});
        }
    }

    for (auto &target : rebalanceTargets)
        rebalanceFileReplication(target.first, target.second);
}

void Server::rebalanceFileReplication(string filePath, map<string, std::shared_ptr<ConnectionSession>> fileHoldersMap)
{
    vector<uint8_t> fileData;
    if (fileHoldersMap.empty())
        return;

    auto sourceSession = fileHoldersMap.begin()->second;
    if (!sourceSession)
        return;

    Message sourceMessage = sourceSession->readFile(filePath, fileData);
    if (!sourceMessage.isAck())
        return;

    vector<pair<string, std::shared_ptr<ConnectionSession>>> candidates;
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        for (auto &entry : dataNodeEndpointMap)
        {
            if (fileHoldersMap.find(entry.first) == fileHoldersMap.end() && entry.second && entry.second->isAlive())
                candidates.push_back(entry);
        }
    }

    for (auto &candidate : candidates)
    {
        Message response = candidate.second->createFile(filePath, FileManager::getInstance().getFileVersion(filePath), fileData);
        if (response.isAck())
        {
            fileHoldersMap[candidate.first] = candidate.second;
            if (fileHoldersMap.size() >= static_cast<size_t>(replicationFactor))
                break;
        }
    }

    if (fileHoldersMap.size() >= static_cast<size_t>(replicationFactor))
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        dataNodeReplicationMap[filePath] = std::move(fileHoldersMap);
    }
}

bool Server::propagateCreateFile(string filePath, int version, const string &originProcessName, const vector<uint8_t> &fileData)
{
    vector<pair<string, std::shared_ptr<ConnectionSession>>> datanodes;
    vector<pair<string, std::shared_ptr<ConnectionSession>>> clients;

    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        for (auto &entry : dataNodeEndpointMap)
            datanodes.push_back(entry);
        for (auto &entry : clientEndpointMap)
            clients.push_back(entry);
    }

    bool ok = true;
    vector<future<Message>> futures;
    futures.reserve(datanodes.size());

    for (auto &datanode : datanodes)
    {
        if (!datanode.second || !datanode.second->isAlive())
            continue;

        futures.push_back(requestPool_.submit([session = datanode.second, filePath, version, fileData]() {
            return session->createFile(filePath, version, fileData);
        }));
    }

    size_t futureIndex = 0;
    for (auto &datanode : datanodes)
    {
        if (!datanode.second || !datanode.second->isAlive())
            continue;

        Message response = futures[futureIndex++].get();
        if (!response.isAck())
            ok = false;

        if (response.isAck())
        {
            std::unique_lock<std::shared_mutex> lock(stateMutex);
            dataNodeReplicationMap[filePath][datanode.first] = datanode.second;
        }
    }

    if (ok)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        fileLockMap.insert({filePath, FileLock(filePath)});
        fileTimeMap[filePath] = chrono::system_clock::now().time_since_epoch().count();
    }

    for (auto &client : clients)
    {
        if (!client.second || client.first == originProcessName)
            continue;
        client.second->post([filePath, version](SquidProtocol &protocol) {
            protocol.createFile(filePath, version);
        });
    }

    if (ok)
        FileManager::getInstance().setFileVersion(filePath, version);

    return ok;
}

bool Server::propagateUpdateFile(string filePath, int version, const string &originProcessName, const vector<uint8_t> &fileData)
{
    vector<pair<string, std::shared_ptr<ConnectionSession>>> datanodes;
    vector<pair<string, std::shared_ptr<ConnectionSession>>> clients;

    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        auto it = dataNodeReplicationMap.find(filePath);
        if (it != dataNodeReplicationMap.end())
        {
            for (auto &entry : it->second)
                datanodes.push_back(entry);
        }
        for (auto &entry : clientEndpointMap)
            clients.push_back(entry);
    }

    bool ok = true;
    vector<future<Message>> futures;
    futures.reserve(datanodes.size());

    for (auto &datanode : datanodes)
    {
        if (!datanode.second || !datanode.second->isAlive())
            continue;

        futures.push_back(requestPool_.submit([session = datanode.second, filePath, version, fileData]() {
            return session->updateFile(filePath, version, fileData);
        }));
    }

    size_t futureIndex = 0;
    for (auto &datanode : datanodes)
    {
        if (!datanode.second || !datanode.second->isAlive())
            continue;

        Message response = futures[futureIndex++].get();
        if (!response.isAck())
            ok = false;
    }

    for (auto &client : clients)
    {
        if (!client.second || client.first == originProcessName)
            continue;
        client.second->post([filePath, version](SquidProtocol &protocol) {
            protocol.updateFile(filePath, version);
        });
    }

    if (ok)
        FileManager::getInstance().setFileVersion(filePath, version);

    return ok;
}

void Server::propagateDeleteFile(string filePath, const string &originProcessName)
{
    vector<pair<string, std::shared_ptr<ConnectionSession>>> datanodes;
    vector<pair<string, std::shared_ptr<ConnectionSession>>> clients;

    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        auto it = dataNodeReplicationMap.find(filePath);
        if (it != dataNodeReplicationMap.end())
        {
            for (auto &entry : it->second)
                datanodes.push_back(entry);
        }
        for (auto &entry : clientEndpointMap)
            clients.push_back(entry);
    }

    for (auto &datanode : datanodes)
    {
        if (datanode.second && datanode.second->isAlive())
            requestPool_.submit([session = datanode.second, filePath]() { return session->deleteFile(filePath); });
    }

    for (auto &client : clients)
    {
        if (!client.second || client.first == originProcessName)
            continue;
        client.second->post([filePath](SquidProtocol &protocol) {
            protocol.deleteFile(filePath);
        });
    }

    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        fileLockMap.erase(filePath);
        dataNodeReplicationMap.erase(filePath);
    }

    FileManager::getInstance().deleteFileAndVersion(filePath);
}

void Server::propagateCreateFile(string filePath, const string &originProcessName)
{
    (void)propagateCreateFile(filePath, FileManager::getInstance().getFileVersion(filePath), originProcessName, {});
}

void Server::propagateCreateFile(string filePath, int version, const string &originProcessName)
{
    (void)propagateCreateFile(filePath, version, originProcessName, {});
}

void Server::propagateUpdateFile(string filePath, const string &originProcessName)
{
    (void)originProcessName;
    (void)filePath;
}

void Server::propagateUpdateFile(string filePath, int version, const string &originProcessName)
{
    (void)originProcessName;
    (void)version;
    (void)filePath;
}

void Server::printMap(map<string, long long> &map, string name)
{
    cout << "[SERVER]: " << name << endl;
    for (auto &pair : map)
        cout << pair.first << " => " << pair.second << endl;
}

void Server::printMap(map<string, std::shared_ptr<ConnectionSession>> &map, string name)
{
    cout << "[SERVER]: " << name << endl;
    for (auto &pair : map)
        cout << pair.first << " => " << (pair.second ? pair.second->toString() : "<null>") << endl;
}

void Server::printMap(map<string, FileLock> &map, string name)
{
    cout << "[SERVER]: " << name << endl;
    for (auto &pair : map)
        cout << pair.first << " => " << pair.second.getFilePath() << " : " << pair.second.isLocked() << endl;
}

void Server::printMap(map<string, map<string, std::shared_ptr<ConnectionSession>>> &map, string name)
{
    cout << "[SERVER]: " << name << endl;
    for (auto &pair : map)
    {
        cout << pair.first << " => ";
        for (auto &innerPair : pair.second)
            cout << innerPair.first << " : " << (innerPair.second ? innerPair.second->toString() : "<null>") << ", ";
        cout << endl;
    }
}

std::vector<std::string> Server::pickDataNodesLocked(size_t count)
{
    std::shared_lock<std::shared_mutex> lock(stateMutex);
    std::vector<std::string> nodes;
    nodes.reserve(dataNodeEndpointMap.size());
    for (auto &entry : dataNodeEndpointMap)
        nodes.push_back(entry.first);

    std::vector<std::string> selected;
    if (nodes.empty())
        return selected;

    roundRobinCursor %= nodes.size();
    for (size_t i = 0; i < count && i < nodes.size(); ++i)
        selected.push_back(nodes[(roundRobinCursor + i) % nodes.size()]);

    roundRobinCursor = (roundRobinCursor + count) % nodes.size();
    return selected;
}

std::shared_ptr<ConnectionSession> Server::getDataNodeSessionLocked(const std::string &name)
{
    std::shared_lock<std::shared_mutex> lock(stateMutex);
    auto it = dataNodeEndpointMap.find(name);
    return it == dataNodeEndpointMap.end() ? nullptr : it->second;
}

std::shared_ptr<ConnectionSession> Server::getClientSessionLocked(const std::string &name)
{
    std::shared_lock<std::shared_mutex> lock(stateMutex);
    auto it = clientEndpointMap.find(name);
    return it == clientEndpointMap.end() ? nullptr : it->second;
}
