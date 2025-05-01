#include "peer.hpp"

Peer::Peer() {};

Peer::Peer(std::string nodeType, std::string processName) : Peer(SERVER_IP, SERVER_PORT, nodeType, processName) {}
Peer::Peer(int port, std::string nodeType, std::string processName) : Peer(SERVER_IP, port, nodeType, processName) {}

Peer::Peer(const char *server_ip, int port, std::string nodeType, std::string processName)
{
    this->nodeType = nodeType;
    this->processName = processName;
    this->server_ip = server_ip;
    this->port = port;

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
    // create socket
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

    // connect to server
    while (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << nodeType + ": connection to server failed, retrying..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    std::cout << nodeType + ": Connected to server...\n";
    squidProtocol.setSocket(socket_fd);
    squidProtocol.setIsAlive(true);
}

void Peer::reconnect()
{
    close(socket_fd);
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("[Peer]: Socket creation failed");
        exit(EXIT_FAILURE);
    }

    while (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << nodeType + ": connection to server failed, retrying..." << std::endl;
    }
    std::cout << nodeType + ": Reconnected to server...\n";
    squidProtocol.setSocket(socket_fd);
    squidProtocol.setIsAlive(true);
}

void Peer::handleRequest(Message mex)
{
    try
    {
        std::cout << nodeType << ": Received message: " << mex.keyword << std::endl;
        squidProtocol.responseDispatcher(mex);
    }
    catch (std::exception &e)
    {
        std::cerr << nodeType + ": Error receiving message: " << e.what() << std::endl;
    }
}
