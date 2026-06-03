#include "peer.hpp"
#include <memory>

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
    this->squidProtocol = SquidProtocol();
}

Peer::~Peer()
{
}

void Peer::connectToServer()
{
    auto channel = std::make_shared<TCPConnectorChannel>(server_ip, port, timeoutSeconds, 3);
    std::cout << nodeType + ": Connected to server...\n";
    squidProtocol = SquidProtocol(fileManager, channel, nodeType, processName);
}

void Peer::reconnect()
{
    auto channel = std::make_shared<TCPConnectorChannel>(server_ip, port, timeoutSeconds, 3);
    std::cout << nodeType + ": Reconnected to server...\n";
    squidProtocol = SquidProtocol(fileManager, channel, nodeType, processName);
}

void Peer::handleRequest(const Message &msg)
{
    try
    {
        std::cout << nodeType << ": Received message: " << msg.toString() << std::endl;
        squidProtocol.responseDispatcher(msg);
    }
    catch (const std::exception &e)
    {
        std::cerr << nodeType + ": Error handling message: " << e.what() << std::endl;
    }
}
