#include "squidProtocolCommunicator.hpp"

SquidProtocolCommunicator::SquidProtocolCommunicator() {};
SquidProtocolCommunicator::SquidProtocolCommunicator(int socket_fd, std::string nodeType, std::string processName){
    this->socket_fd = socket_fd;
    this->processName = processName;
    this->nodeType = nodeType;
    this->fileTransfer = FileTransfer();
    this->formatter = SquidProtocolFormatter(nodeType);
}

int SquidProtocolCommunicator::getSocket()
{
    return socket_fd;
}

// ----------------------------
// --------- RECEIVER ---------
// ----------------------------

Message SquidProtocolCommunicator::receiveAndParseMessage()
{
    std::string receivedMessage = receiveMessageWithLength();
    std::cout << nodeType + ": Received message: " << receivedMessage << std::endl;
    return this->formatter.parseMessage(receivedMessage);
}

void checkBytesRead(ssize_t bytesRead, std::string nodeType)
{
    if (bytesRead == 0)
    {
        std::cerr << nodeType + ": Connection closed by peer" << std::endl;
        throw std::runtime_error("Connection closed by peer");
    }
    else if (bytesRead < 0)
    {
        std::string msg = std::string(nodeType) + ": Failed to receive message";
        perror(msg.c_str());
        throw std::runtime_error("Failed to receive message");
    }
}

std::string SquidProtocolCommunicator::receiveMessageWithLength()
{
    // Read the length of the message
    uint32_t messageLength;
    ssize_t bytesRead = recv(socket_fd, &messageLength, sizeof(messageLength), 0);
    checkBytesRead(bytesRead, nodeType);

    messageLength = ntohl(messageLength);
    std::cout << nodeType + "[INFO]: Expecting message of length: " << messageLength << std::endl;

    // Read the actual message
    char *buffer = new char[messageLength + 1];
    bytesRead = recv(socket_fd, buffer, messageLength, 0);
    checkBytesRead(bytesRead, nodeType);

    buffer[messageLength] = '\0';
    std::string message(buffer);
    delete[] buffer;

    // std::cout << "[INFO]: Received message: " << message << std::endl;
    return message;
}

// --------------------------
// --------- SENDER ---------
// --------------------------

void SquidProtocolCommunicator::sendMessage(std::string message)
{
    sendMessageWithLength(message);
}

void SquidProtocolCommunicator::sendMessageWithLength(std::string &message)
{
    uint32_t messageLength = htonl(message.size());

    // Send the length of the message
    if (send(socket_fd, &messageLength, sizeof(messageLength), 0) < 0)
    {
        std::cerr << nodeType + "[ERROR]: Failed to send message length" << std::endl;
        return;
    }

    // Send the actual message
    if (send(socket_fd, message.c_str(), message.size(), 0) < 0)
    {
        std::cerr << nodeType + "[ERROR]: Failed to send message" << std::endl;
        return;
    }

    std::cout << nodeType + "[INFO]: Sent message with length: " << message.size() << std::endl;
}

void SquidProtocolCommunicator::transferFile(std::string filePath, Message response)
{
    // std::cout << nodeType + ": trying transfering file" << std::endl;
    if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
    else
        perror(std::string("Error while transfering file: " + filePath).c_str());
    return;
}
