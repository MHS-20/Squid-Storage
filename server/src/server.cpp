#include "server.hpp"
using namespace std;

Server::Server() : Server(DEFAULT_PORT, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port) : Server(port, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port, int replicationFactor)
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

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    fileTransfer = FileTransfer();
    fileLockMap = map<string, FileLock>();
    fileTimeMap = map<string, long long>();

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
        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            cerr << "[SERVER]: Select failed" << endl;
            continue;
        }

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
        // primaryProtocol.response(string("ACK"));
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
    }

    switch (mex.keyword)
    {
    case CREATE_FILE:
        clientProtocol.requestDispatcher(mex);
        propagateCreateFile(mex.args["filePath"], clientProtocol);
        FileManager::getInstance().deleteFile(mex.args["filePath"]);
        break;
    case READ_FILE:
        getFileFromDataNode(mex.args["filePath"], clientProtocol);
        clientProtocol.requestDispatcher(mex);
        FileManager::getInstance().deleteFile(mex.args["filePath"]);
        break;
    case UPDATE_FILE:
        clientProtocol.requestDispatcher(mex);
        propagateUpdateFile(mex.args["filePath"], clientProtocol);
        FileManager::getInstance().deleteFile(mex.args["filePath"]);
        break;
    case DELETE_FILE:
        clientProtocol.requestDispatcher(mex);
        propagateDeleteFile(mex.args["filePath"], clientProtocol);
        dataNodeReplicationMap.erase(mex.args["filePath"]);
        FileManager::getInstance().deleteFile(mex.args["filePath"]);
        break;
    case SYNC_STATUS:
        cout << "SERVER: received sync status request\n";
        clientProtocol.response(fileTimeMap);
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
    // printMap(fileTimeMap, "File Time Map");
    // printMap(dataNodeReplicationMap, "DataNode Replication Map");
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
        Message files = datanodeEndpoint.second.listFiles(); // <filename; last write time>
        for (auto &file : files.args)
        {
            if (fileLockMap.find(file.first) == fileLockMap.end())
            {
                fileLockMap[file.first] = FileLock(file.first);
                fileTimeMap[file.first] = stoll(file.second);
            }

            if (dataNodeReplicationMap.find(file.first) == dataNodeReplicationMap.end())
            {
                dataNodeReplicationMap[file.first].insert(datanodeEndpoint);
            }
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

    SquidProtocol dataNodeHolderProtocol = fileHoldersMap.begin()->second;
    Message mex = dataNodeHolderProtocol.readFile(filePath);
    if (mex.args["ACK"] != "ACK")
        cerr << "Error while retriving file from datanode";
    else
        cout << "Retrived file from datanode holder" << endl;
}

void Server::propagateUpdateFile(string filePath, SquidProtocol clientProtocol)
{
    /*
    for (auto &client : clientEndpointMap)
    {
        if (client.second.first.getSocket() != clientProtocol.getSocket())
            client.second.second.updateFile(filePath);
    }
    */
    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.updateFile(filePath);

    fileTimeMap[filePath] = chrono::system_clock::now().time_since_epoch().count();
}

void Server::propagateDeleteFile(string filePath, SquidProtocol clientProtocol)
{
    /*
    for (auto &client : clientEndpointMap)
    {
        if (client.second.first.getSocket() != clientProtocol.getSocket())
            client.second.second.deleteFile(filePath);
    }
    */
    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.deleteFile(filePath);

    fileLockMap.erase(filePath);
    fileTimeMap.erase(filePath);
    dataNodeReplicationMap.erase(filePath);
}

void Server::propagateCreateFile(string filePath, SquidProtocol clientProtocol)
{ // round robin replication
    lock_guard<mutex> lock(mapMutex);
    auto fileHoldersMap = map<string, SquidProtocol>();

    if (dataNodeEndpointMap.empty())
        return;

    for (int i = 0; i < replicationFactor; i++)
    {
        if (endpointIterator == dataNodeEndpointMap.end())
        {
            endpointIterator = dataNodeEndpointMap.begin();
        }

        fileHoldersMap.insert({endpointIterator->first, endpointIterator->second});
        endpointIterator++;
    }

    cout << "iterated" << endl;
    dataNodeReplicationMap.insert({filePath, fileHoldersMap});

    fileLockMap.insert({filePath, FileLock(filePath)});
    fileTimeMap.insert({filePath, chrono::system_clock::now().time_since_epoch().count()});

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.createFile(filePath);
    /*
    for (auto &client : clientEndpointMap)
    {
        if (client.second.first.getSocket() != clientProtocol.getSocket())
            client.second.second.createFile(filePath);
    }
    */
}

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