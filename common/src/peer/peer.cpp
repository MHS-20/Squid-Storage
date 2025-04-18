#include "peer.hpp"

Peer::Peer() {};

Peer::Peer(std::string nodeType, std::string processName)
{
    this->nodeType = nodeType;
    this->processName = processName;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("[Peer]: Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        perror("[Peer]: Invalid address");
        exit(EXIT_FAILURE);
    }

    this->fileTransfer = FileTransfer();
    this->squidProtocol = SquidProtocol(socket_fd, nodeType, processName);
}

Peer::Peer(const char *server_ip, int port, std::string nodeType, std::string processName)
{
    this->nodeType = nodeType;
    this->processName = processName;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("[Peer]: Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
    {
        perror("[Peer]: Invalid address");
        exit(EXIT_FAILURE);
    }

    this->fileTransfer = FileTransfer();
    this->squidProtocol = SquidProtocol(socket_fd, nodeType, processName);
}

Peer::~Peer()
{
    close(socket_fd);
}

int Peer::getSocket()
{
    return socket_fd;
}

void Peer::connectToServer()
{
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << nodeType + ": connection to server failed" << std::endl;
        exit(EXIT_FAILURE);
    }
    std::cout << nodeType + ": Connected to server...\n";
}

void Peer::handleRequest(Message mex)
{
    try
    {
        std::cout << nodeType << ": Received message: " << mex.keyword << std::endl;
    }
    catch (std::exception &e)
    {
        std::cerr << nodeType + ": Error receiving message: " << e.what() << std::endl;
    }
    squidProtocol.responseDispatcher(mex);
}
