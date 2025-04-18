#include "server.hpp"

Server::Server() : Server(DEFAULT_PORT, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port) : Server(port, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port, int replicationFactor) // : fileManager(FileManager::getInstance())
{
    this->port = port;
    this->replicationFactor = replicationFactor;
    this->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    std::cout << "[SERVER]: Initializing..." + server_fd << std::endl;
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

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    fileTransfer = FileTransfer();
    fileLockMap = std::map<std::string, FileLock>();
    fileTimeMap = std::map<std::string, long long>();

    clientEndpointMap = std::map<std::string, SquidProtocol>();
    dataNodeEndpointMap = std::map<std::string, SquidProtocol>();
    dataNodeReplicationMap = std::map<std::string, std::map<std::string, SquidProtocol>>();

    endpointIterator = dataNodeEndpointMap.begin();
    // readsLoadBalancingIterator = dataNodeReplicationMap.begin();
}

Server::~Server()
{
    close(server_fd);
}

void Server::run()
{
    std::cout << "[SERVER]: Server Starting..." << std::endl;
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        return;
    }

    listen(server_fd, 3);
    std::cout << "[SERVER]: Server listening on " << this->port << "...\n";

    while (true)
    {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0)
        {
            perror("[SERVER]: Accept failed");
            exit(EXIT_FAILURE);
        }

        std::cout << "Accepted connection: " << new_socket << "...\n";

        // new thread to handle connection:
        std::thread connectionThread(&Server::handleConnection, this, new_socket);
        connectionThread.detach();
        std::cout << "[SERVER]: New thread created to handle connection: " << new_socket << std::endl;
    }
}

// ------------------------------
// --- COMMUNICATION HANDLING ---
// ------------------------------
void Server::handleConnection(int new_socket)
{
    Message mex;
    SquidProtocol clientProtocol = SquidProtocol(new_socket, "[SERVER]", "SERVER");
    if (!identify(clientProtocol))
        return;

    std::cout << "Checking for messages ...\n";
    while (true)
    {
        std::cout << "[SERVER]: Waiting for messages..." << std::endl;
        if (clientProtocol.getSocket() < 0)
        {
            std::cout << "[SERVER]: Closing & Terminating" << std::endl;
            break;
        }

        try
        {
            mex = clientProtocol.receiveAndParseMessage();
            std::cout << std::this_thread::get_id();
            std::cout << "[SERVER]: Received message: " + mex.keyword << std::endl;
        }
        catch (std::exception &e)
        {
            std::cerr << "[SERVER]: Error receiving message: " << e.what() << std::endl;
            break;
        }

        switch (mex.keyword)
        {
        case CREATE_FILE:
            clientProtocol.requestDispatcher(mex);
            createFileOnDataNodes(mex.args["filePath"], clientProtocol);
            FileManager::getInstance().deleteFile(mex.args["filePath"]);
            break;
        case READ_FILE:
            getFileFromDataNode(mex.args["filePath"], clientProtocol);
            clientProtocol.requestDispatcher(mex);
            FileManager::getInstance().deleteFile(mex.args["filePath"]);
            break;
        case UPDATE_FILE:
            clientProtocol.requestDispatcher(mex);
            updateFileOnDataNodes(mex.args["filePath"], clientProtocol);
            FileManager::getInstance().deleteFile(mex.args["filePath"]);
            break;
        case DELETE_FILE:
            clientProtocol.requestDispatcher(mex);
            deleteFileFromDataNodes(mex.args["filePath"], clientProtocol);
            dataNodeReplicationMap.erase(mex.args["filePath"]);
            FileManager::getInstance().deleteFile(mex.args["filePath"]);
            break;
        case SYNC_STATUS:
            std::cout << "SERVER: received sync status request\n";
            clientProtocol.response(fileTimeMap);
            break;
        case ACQUIRE_LOCK:
            std::cout << "[SERVER]: received acquire lock request for " << mex.args["filePath"] << std::endl;
            clientProtocol.response(this->acquireLock(mex.args["filePath"]));
            break;
        case RELEASE_LOCK:
            this->releaseLock(mex.args["filePath"]);
            clientProtocol.response(std::string("ACK"));
            break;
        default:
            clientProtocol.requestDispatcher(mex);
        }

        std::cout << "[SERVER]: Request dispatched" << std::endl;

        printMap(fileLockMap, "File Lock Map");
        printMap(fileTimeMap, "File Time Map");
        printMap(dataNodeReplicationMap, "DataNode Replication Map");
    }
};

bool Server::identify(SquidProtocol clientProtocol)
{
    Message mex = clientProtocol.identify();
    std::cout << "[SERVER]: Identity received from peer: " + mex.args["processName"] << std::endl;

    if (mex.args["nodeType"] == "CLIENT")
    {
        clientEndpointMap[mex.args["processName"]] = clientProtocol;
        printMap(clientEndpointMap, "Client Endpoint Map");
        std::cout << std::this_thread::get_id();
        std::cout << " attached to client ";
    }
    else if (mex.args["nodeType"] == "DATANODE")
    {
        dataNodeEndpointMap[mex.args["processName"]] = clientProtocol;
        printMap(dataNodeEndpointMap, "DataNode Endpoint Map");
        std::cout << std::this_thread::get_id();
        std::cout << " attached to datanode";

        std::cout << "[SERVER]: Building file map..." << std::endl;
        buildFileLockMap();
        printMap(fileLockMap, "File Lock Map");
        printMap(fileTimeMap, "File Time Map");
        printMap(dataNodeReplicationMap, "DataNode Replication Map");
        std::cout << "exiting...";
    }
    else
    {
        std::cout << "[SERVER]: Unknown node type\n";
        return false;
    }

    clientProtocol.response(std::string("ACK"));
    std::cout << "[SERVER]: Ack sent to client" << std::endl;

    if (mex.args["nodeType"] == "CLIENT")
        return true;
    else
        return false;
}

// -----------------------
// ---- FILE LOCKING -----
// -----------------------

bool Server::acquireLock(std::string path)
{
    if (fileLockMap.find(path) == fileLockMap.end())
    {
        std::cout << "[SERVER]: File not found in file map... updating file map" << std::endl;
        buildFileLockMap();
        if (fileLockMap.find(path) == fileLockMap.end())
        {
            std::cout << "[SERVER]: File not found" << std::endl;
            return false;
        }
        return false;
    }

    if (!fileLockMap[path].isLocked())
    {
        fileLockMap[path].setIsLocked(true);
        return true;
    }
    else
    {
        return false;
    }
}

bool Server::releaseLock(std::string path)
{
    if (fileLockMap.find(path) == fileLockMap.end())
    {
        std::cout << "[SERVER]: File not found in file map... updating file map" << std::endl;
        buildFileLockMap();
        if (fileLockMap.find(path) == fileLockMap.end())
        {
            std::cout << "[SERVER]: File not found" << std::endl;
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

// -------------------------------
// ---- DATANODE REPLICATION -----
// -------------------------------

void Server::buildFileLockMap()
{
    std::cout << "[SERVER]: Building file map..." << std::endl;
    for (auto &datanodeEndpoint : dataNodeEndpointMap)
    {
        std::cout << "[SERVER]: Building file map from datanode: " + datanodeEndpoint.first << std::endl;
        Message files = datanodeEndpoint.second.listFiles(); // <filename; last write time>
        for (auto &file : files.args)
        {
            if (fileLockMap.find(file.first) == fileLockMap.end())
            {
                fileLockMap[file.first] = FileLock(file.first);
                fileTimeMap[file.first] = std::stoll(file.second);
            }

            if (dataNodeReplicationMap.find(file.first) == dataNodeReplicationMap.end())
            {
                dataNodeReplicationMap[file.first].insert(datanodeEndpoint);
            }
            std::cout << "[SERVER]: File: " + file.first + " added to datanode: " + datanodeEndpoint.first << std::endl;
        }
    }
    std::cout << "[SERVER]: File map built successfully" << std::endl;
}

void Server::getFileFromDataNode(std::string filePath, SquidProtocol clientProtocol)
{
    std::cout << "retriving file " + filePath << std::endl;
    if (dataNodeReplicationMap.find(filePath) == dataNodeReplicationMap.end())
    {
        std::cout << "[SERVER]: File not found in datanode replication map" << std::endl;
        return;
    }

    std::cout << "file found on datanode" << std::endl;
    auto &fileHoldersMap = dataNodeReplicationMap[filePath];
    // clientProtocol.responseDispatcher(fileHoldersMap.begin()->second.readFile(filePath));

    SquidProtocol dataNodeHolderProtocol = fileHoldersMap.begin()->second;
    Message mex = dataNodeHolderProtocol.readFile(filePath);
    if (mex.args["ACK"] != "ACK")
        std::cerr << "Error while retriving file from datanode";
    else
        std::cout << "Retrived file from datanode holder";

    // if (readsLoadBalancingIterator == fileHoldersMap.end())
    //     readsLoadBalancingIterator = fileHoldersMap.begin();
    // clientProtocol.responseDispatcher(readsLoadBalancingIterator->second.readFile(filePath));
    // readsLoadBalancingIterator++;
}

void Server::updateFileOnDataNodes(std::string filePath, SquidProtocol clientProtocol)
{
    for (auto &client : clientEndpointMap)
    {
        if (client.second.getSocket() != clientProtocol.getSocket())
            client.second.updateFile(filePath);
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.updateFile(filePath);

    fileTimeMap[filePath] = std::chrono::system_clock::now().time_since_epoch().count();
}

void Server::deleteFileFromDataNodes(std::string filePath, SquidProtocol clientProtocol)
{
    for (auto &client : clientEndpointMap)
    {
        if (client.second.getSocket() != clientProtocol.getSocket())
            client.second.deleteFile(filePath);
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.deleteFile(filePath);

    fileLockMap.erase(filePath);
    fileTimeMap.erase(filePath);
    dataNodeReplicationMap.erase(filePath);
}

void Server::createFileOnDataNodes(std::string filePath, SquidProtocol clientProtocol)
{ // round robin replication
    std::lock_guard<std::mutex> lock(mapMutex);
    auto fileHoldersMap = std::map<std::string, SquidProtocol>();

    if (dataNodeEndpointMap.empty())
        return;

    for (int i = 0; i < replicationFactor; i++)
    {
        std::cout << "checking iterator" << std::endl;
        if (endpointIterator == dataNodeEndpointMap.end())
        {
            endpointIterator = dataNodeEndpointMap.begin();
        }

        std::cout << "iterating" << std::endl;
        fileHoldersMap.insert({endpointIterator->first, endpointIterator->second});
        std::cout << "inc iterator" << std::endl;
        endpointIterator++;
    }

    std::cout << "iterated" << std::endl;
    dataNodeReplicationMap.insert({filePath, fileHoldersMap});
    // readsLoadBalancingIterator = dataNodeReplicationMap[filePath].begin();

    fileLockMap.insert({filePath, FileLock(filePath)});
    fileTimeMap.insert({filePath, std::chrono::system_clock::now().time_since_epoch().count()});

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.createFile(filePath);

    for (auto &client : clientEndpointMap)
    {
        if (client.second.getSocket() != clientProtocol.getSocket())
            client.second.createFile(filePath);
    }
}

// -----------------------
// ------ PRINT MAPS -----
// -----------------------

void Server::printMap(std::map<std::string, SquidProtocol> &map, std::string name)
{
    std::cout << "[SERVER]: " << name << std::endl;
    for (auto &pair : map)
    {
        std::cout << pair.first << " => " << pair.second.toString() << std::endl;
    }
}

void Server::printMap(std::map<std::string, FileLock> &map, std::string name)
{
    std::cout << "[SERVER]: " << name << std::endl;
    for (auto &pair : map)
    {
        std::cout << pair.first << " => " << pair.second.getFilePath() << " : " << pair.second.isLocked() << std::endl;
    }
}

void Server::printMap(std::map<std::string, std::map<std::string, SquidProtocol>> &map, std::string name)
{
    std::cout << "[SERVER]: " << name << std::endl;
    for (auto &pair : map)
    {
        std::cout << pair.first << " => ";
        for (auto &innerPair : pair.second)
        {
            std::cout << innerPair.first << " : " << innerPair.second.toString() << ", ";
        }
        std::cout << std::endl;
    }
}

void Server::printMap(std::map<std::string, long long> &map, std::string name)
{
    std::cout << "[SERVER]: " << name << std::endl;
    for (auto &pair : map)
    {
        std::cout << pair.first << " => " << pair.second << std::endl;
    }
}
