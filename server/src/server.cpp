#include "server.hpp"
#include <thread>

Server::Server() : Server(DEFAULT_PORT) {}

Server::Server(int port)
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
    fileMap = std::map<std::string, int>();
    clientEndpointMap = std::map<std::string, int>();
    dataNodeEndpointMap = std::map<std::string, int>();
}

Server::~Server()
{
    close(server_fd);
}

int Server::getSocket()
{
    return server_fd;
}

void Server::start()
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
        this->handleClient(new_socket);
    }
}

void Server::handleClient(int client_socket)
{
    SquidProtocol clientProtocol = SquidProtocol(client_socket, "server", "SERVER");
    std::cout << "Checking for messages ...\n";

    Message mex = clientProtocol.identify();
    std::cout << "[SERVER]: Identity received from client: " + mex.args["processName"] << std::endl;

    clientProtocol.response(std::string("ACK"));
    std::cout << "[SERVER]: Ack sent to client" << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(3));

    while (true)
    {
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
        clientProtocol.requestDispatcher(mex);
    }
}

void printMap(std::map<std::string, int> &map, std::string name)
{
    std::cout << "[SERVER]: " << name << std::endl;
    for (auto &pair : map)
    {
        std::cout << pair.first << " => " << pair.second << std::endl;
    }
}

void Server::identifyConnection(int new_socket)
{
    char buffer[BUFFER_SIZE] = {0};
    char connection_ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &address.sin_addr, connection_ip, INET_ADDRSTRLEN);
    std::string new_endpoint = std::string(connection_ip) + ":" + std::to_string(ntohs(address.sin_port));
    std::cout << "[SERVER]: Connection from " << new_endpoint << std::endl;

    read(new_socket, buffer, sizeof(buffer));
    std::cout << "[SERVER]: Connection identified as: " << buffer << std::endl;

    if (strcmp(buffer, "CLIENT") == 0)
    {
        clientEndpointMap[new_endpoint] = new_socket;
    }
    else if (strcmp(buffer, "DATANODE") == 0)
    {
        dataNodeEndpointMap[new_endpoint] = new_socket;
    }
    else
    {
        std::cout << "[SERVER]: Unknown client type\n";
    }

    printMap(clientEndpointMap, "CLIENTS MAP");
    printMap(dataNodeEndpointMap, "DATANODES MAP");
}

void Server::handleClientMessage(int client_socket)
{
    char buffer[BUFFER_SIZE] = {0};

    read(client_socket, buffer, sizeof(buffer));
    std::cout << "[SERVER]: Received: " << buffer << std::endl;

    const char *message = "Hello from server";
    send(client_socket, message, strlen(message), 0);
    std::cout << "[SERVER]: Reply sent\n";
}

/* ---- FILE TRANSFER API ----- */
void Server::sendFile(int client_socket, const char *filepath)
{
    this->fileTransfer.sendFile(client_socket, "[SERVER]", filepath);
}

void Server::receiveFile(int client_socket, const char *outputpath)
{
    this->fileTransfer.receiveFile(client_socket, "[SERVER]", outputpath);
}
