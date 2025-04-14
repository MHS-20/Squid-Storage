#include "server.hpp"

Server::Server() : Server(DEFAULT_PORT, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port) : Server(port, DEFAULT_REPLICATION_FACTOR) {}

Server::Server(int port, int replicationFactor) : fileManager(FileManager::getInstance())
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
    // fileMap = FileManager::getInstance().getFileMap();
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

void printMap(std::map<std::string, SquidProtocol> &map, std::string name)
{
    std::cout << "[SERVER]: " << name << std::endl;
    for (auto &pair : map)
    {
        std::cout << pair.first << " => " << pair.second.toString() << std::endl;
    }
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

void Server::handleConnection(int new_socket)
{

    Message mex;
    SquidProtocol clientProtocol = SquidProtocol(new_socket,"[SERVER]", "SERVER");
    identify(clientProtocol);

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
            break;
        case READ_FILE:
            getFileFromDataNode(mex.args["filePath"], clientProtocol);
            clientProtocol.requestDispatcher(mex);
            break;
        case UPDATE_FILE:
            clientProtocol.requestDispatcher(mex);
            //updateFileOnDataNodes(mex.args["filePath"], clientProtocol);
            //dataNodeReplicationMap.erase(mex.args["filePath"]);
            break;
        case DELETE_FILE:
            clientProtocol.requestDispatcher(mex);
            deleteFileFromDataNodes(mex.args["filePath"], clientProtocol);
            dataNodeReplicationMap.erase(mex.args["filePath"]);
            break;
        }
        std::cout << "[SERVER]: Request dispatched" << std::endl;
    }
};

void Server::identify(SquidProtocol clientProtocol)
{
    Message mex = clientProtocol.identify();
    std::cout << "[SERVER]: Identity received from peer: " + mex.args["processName"] << std::endl;

    if (mex.args["nodeType"] == "CLIENT")
    {
        clientEndpointMap[mex.args["processName"]] = clientProtocol;
        printMap(clientEndpointMap, "Client Endpoint Map");
    }
    else if (mex.args["nodeType"] == "DATANODE")
    {
        // dataNodeReplicationMap[mex.args["processName"]] = std::map<std::string, SquidProtocol>();
        dataNodeEndpointMap[mex.args["processName"]] = clientProtocol;
        printMap(dataNodeEndpointMap, "DataNode Endpoint Map");
    }
    else
    {
        std::cout << "[SERVER]: Unknown node type\n";
        return;
    }

    clientProtocol.response(std::string("ACK"));
    std::cout << "[SERVER]: Ack sent to client" << std::endl;
}

void Server::getFileFromDataNode(std::string filePath, SquidProtocol clientProtocol)
{
    auto &fileHoldersMap = dataNodeReplicationMap[filePath];
    if (readsLoadBalancingIterator == fileHoldersMap.end())
        readsLoadBalancingIterator = fileHoldersMap.begin();
    clientProtocol.responseDispatcher(readsLoadBalancingIterator->second.readFile(filePath));
    readsLoadBalancingIterator++;
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
}

void Server::deleteFileFromDataNodes(std::string filePath, SquidProtocol clientProtocol)
{
    for (auto &client : clientEndpointMap){
        if (client.second.getSocket() != clientProtocol.getSocket())
            client.second.deleteFile(filePath);
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.deleteFile(filePath);
}

void Server::createFileOnDataNodes(std::string filePath, SquidProtocol clientProtocol)
{ // round robin replication
    // std::cout << "replicating" << std::endl;
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
    this->readsLoadBalancingIterator = dataNodeReplicationMap[filePath].begin();

    for (auto &client : clientEndpointMap)
    {
        if (client.second.getSocket() != clientProtocol.getSocket())
            client.second.createFile(filePath);
    }

    for (auto &datanode : dataNodeReplicationMap[filePath])
        datanode.second.createFile(filePath);
}
