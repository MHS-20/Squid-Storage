#include "squidprotocol.hpp"
#include "squidProtocolPassiveServer.cpp"

class SquidProtocolServer : public SquidProtocol
{

public:
    SquidProtocolServer() : SquidProtocol() {}

    SquidProtocolServer(int socket_fd, std::string nodeType, std::string processName)
    {
        this->socket_fd = socket_fd;
        this->processName = processName;
        this->nodeType = nodeType;

        this->fileTransfer = FileTransfer();
        this->formatter = SquidProtocolFormatter(nodeType);

        this->communicator = SquidProtocolCommunicator(socket_fd, nodeType, processName);
        this->passive = SquidProtocolPassiveServer(socket_fd, nodeType, processName, communicator);
        this->active = SquidProtocolActive(socket_fd, nodeType, processName, communicator);
    }
};