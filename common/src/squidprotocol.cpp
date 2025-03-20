#include "squidprotocol.hpp"

SquidProtocol::SquidProtocol(int socket_fd, std::string processName)
{
    this->socket_fd = socket_fd;
    this->processName = processName;
    this->fileTransfer = FileTransfer();
}

std::string SquidProtocol::createFile(std::string filePath)
{
    this->sendMessage(this->formatter.createFileFormat(filePath));

    Message response = receiveAndParseMessage();
    if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), filePath.c_str());

    return receiveAndParseMessage().args["ACK"];
}

void SquidProtocol::sendMessage(std::string message)
{
    send(this->socket_fd, message.c_str(), message.length(), 0);
}

Message SquidProtocol::receiveAndParseMessage()
{
    if (!recv(this->socket_fd, this->buffer, sizeof(this->buffer), 0))
    {
        perror("Failed to receive message");
    }
    return this->formatter.parseMessage(this->buffer);
}
