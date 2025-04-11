#include "server.hpp"

Server::Server() : Server(DEFAULT_PORT)
{
}

Server::Server(int port) : fileManager(FileManager::getInstance())
{

    this->port = port;
    this->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    std::cout << "[SERVER]: Initializing..." + server_fd << std::endl;
    if (server_fd < 0)
    {
        perror("[SERVER]: Socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        perror("[SERVER]: setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    fileTransfer = FileTransfer();
    // fileManager = FileManager::getInstance();

    // fileMap = fileManager.getFileMap();
    clientEndpointMap = std::map<std::string, SquidProtocolServer>();
    dataNodeEndpointMap = std::map<std::string, SquidProtocolServer>();
}

Server::~Server()
{
    close(server_fd);
}

int Server::getSocket()
{
    return server_fd;
}

void printMap(std::map<std::string, SquidProtocolServer> &map, std::string name)
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
        // this->handleConnection(new_socket);

        // new thread to handle connection:
        std::thread connectionThread(&Server::handleConnection, this, new_socket);
        connectionThread.detach();
        std::cout << "[SERVER]: New thread created to handle connection: " << new_socket << std::endl;
    }
}

void Server::identify(SquidProtocolServer protocol)
{
    Message mex = protocol.active.identify();
    std::cout << "[SERVER]: Identity received from peer: " + mex.args["processName"] << std::endl;

    if (mex.args["nodeType"] == "CLIENT")
    {
        clientEndpointMap[mex.args["processName"]] = protocol;
        printMap(clientEndpointMap, "DataNode Endpoint Map");
    }
    else if (mex.args["nodeType"] == "DATANODE")
    {
        dataNodeEndpointMap[mex.args["processName"]] = protocol;
        printMap(dataNodeEndpointMap, "DataNode Endpoint Map");
    }
    else
    {
        std::cout << "[SERVER]: Unknown node type\n";
        return;
    }

    protocol.passive.response(std::string("ACK"));
    std::cout << "[SERVER]: Ack sent to client" << std::endl;
}

void Server::handleConnection(int new_socket)
{

    Message mex;
    SquidProtocolServer protocol = SquidProtocolServer(new_socket, "[SERVER]", "SERVER");
    identify(protocol);

    std::cout << "Checking for messages ...\n";
    while (true)
    {
        std::cout << "[SERVER]: Waiting for messages..." << std::endl;
        if (protocol.getSocket() < 0) // connection closed
        {
            std::cout << "[SERVER]: Closing & Terminating" << std::endl;
            break;
        }

        try
        {
            mex = protocol.communicator.receiveAndParseMessage();
            std::cout << "[SERVER]: Received message: " + mex.keyword << std::endl;
        }
        catch (std::exception &e)
        {
            std::cerr << "[SERVER]: Error receiving message: " << e.what() << std::endl;
            break;
        }

        protocol.passive.requestDispatcher(mex);
        std::cout << "[SERVER]: Request dispatched" << std::endl;
    }
};
