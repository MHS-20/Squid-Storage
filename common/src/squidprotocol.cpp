#include "squidprotocol.hpp"

SquidProtocol::SquidProtocol(int socket_fd, std::string processName, std::string nodeType)
{
    this->socket_fd = socket_fd;
    this->processName = processName;
    this->fileTransfer = FileTransfer();
    this->fileManager = FileManager();
    this->formatter = SquidProtocolFormatter();
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

std::string SquidProtocol::deleteFile(std::string filePath)
{
    this->sendMessage(this->formatter.deleteFileFormat(filePath));
    return receiveAndParseMessage().args["ACK"];
}

bool SquidProtocol::acquireLock(std::string filePath)
{
    this->sendMessage(this->formatter.acquireLockFormat(filePath));
    return receiveAndParseMessage().args["LOCK"] == "true";
}

std::string SquidProtocol::releaseLock(std::string filePath)
{
    this->sendMessage(this->formatter.releaseLockFormat(filePath));
    return receiveAndParseMessage().args["ACK"];
}

std::string SquidProtocol::heartbeat()
{
    this->sendMessage(this->formatter.heartbeatFormat());
    return receiveAndParseMessage().args["ACK"];
}

std::string SquidProtocol::syncStatus()
{
    this->sendMessage(this->formatter.syncStatusFormat());

    return receiveAndParseMessage().args["ACK"];
}

Message SquidProtocol::identify()
{
    this->sendMessage(this->formatter.identifyFormat());
    return receiveAndParseMessage();
}

void SquidProtocol::response(std::string ack)
{
    this->sendMessage(this->formatter.responseFormat(ack));
}

void SquidProtocol::response(std::string nodeType, std::string processName)
{
    this->sendMessage(this->formatter.responseFormat(nodeType, processName));
}

void SquidProtocol::response(bool lock)
{
    this->sendMessage(this->formatter.responseFormat(lock));
}

void SquidProtocol::dispatcher(Message message)
{
    switch (message.keyword)
    {
    case CREATE_FILE:
        this->response("ACK");
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response("ACK");
        break;
    case TRANSFER_FILE:
        break;
    case READ_FILE:
        break;
    case UPDATE_FILE:
        break;
    case DELETE_FILE:
        this->fileManager.deleteFile(message.args["filePath"]);
        this->response("ACK");
        break;
    case ACQUIRE_LOCK:
        this->response(this->fileManager.acquireLock(message.args["filePath"]));
        break;
    case RELEASE_LOCK:
        this->fileManager.releaseLock(message.args["filePath"]);
        this->response("ACK");
        break;
    case HEARTBEAT:
        this->response("ACK");
        break;
    case SYNC_STATUS:
        break;
    case IDENTIFY:
        this->response(this->nodeType, this->processName);
        break;
    case RESPONSE:
        break;
    default:
        break;
    }
}
