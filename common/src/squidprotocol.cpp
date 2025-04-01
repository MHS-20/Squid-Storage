#include "squidprotocol.hpp"

SquidProtocol::SquidProtocol() {}

SquidProtocol::SquidProtocol(int socket_fd, std::string processName, std::string nodeType)
{
    this->socket_fd = socket_fd;
    this->processName = processName;
    this->nodeType = nodeType;
    this->fileTransfer = FileTransfer();
    this->fileManager = FileManager();
    this->formatter = SquidProtocolFormatter();
}

SquidProtocol::~SquidProtocol() {}

Message SquidProtocol::identify()
{
    std::cout << "Identifying..." << std::endl;
    this->sendMessage(this->formatter.identifyFormat());
    return this->receiveAndParseMessage();
}

std::string SquidProtocol::createFile(std::string filePath)
{
    std::cout << nodeType + ": sending create file request" << std::endl;
    this->sendMessage(this->formatter.createFileFormat(filePath));
    std::cout << nodeType + ": sent create file request" << std::endl;
    Message response = receiveAndParseMessage();
    std::cout << nodeType + ": received create file response" << std::endl;
    transferFile(filePath, response);
    return receiveAndParseMessage().args["ACK"];
}

std::string SquidProtocol::updateFile(std::string filePath)
{
    this->sendMessage(this->formatter.updateFileFormat(filePath));
    Message response = receiveAndParseMessage();
    transferFile(filePath, response);
    return receiveAndParseMessage().args["ACK"];
}

// CHECK
void SquidProtocol::transferFile(std::string filePath, Message response)
{
    std::cout << nodeType + ": trying transfering file" << std::endl;
    if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
    else
        perror(std::string("Error while transfering file: " + filePath).c_str());
    return;
}

std::string SquidProtocol::readFile(std::string filePath)
{
    this->sendMessage(this->formatter.readFileFormat(filePath));
    Message response = receiveAndParseMessage();
    if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
    return receiveAndParseMessage().args["ACK"];
}

void SquidProtocol::sendMessage(std::string message)
{
    std::cout << "[DEBUG " + processName + "]: socket_fd = " << socket_fd << " in " << __FUNCTION__ << std::endl;
    send(this->socket_fd, message.c_str(), message.length(), 0);
}

Message SquidProtocol::receiveAndParseMessage()
{
    // empty the buffer
    // memset(this->buffer, 0, sizeof(this->buffer));
    std::cout << nodeType + ": trying parsing" << std::endl;

    if (recv(this->socket_fd, this->buffer, sizeof(this->buffer), 0) == EOF)
    {
        std::string msg = std::string(nodeType) + ": Failed to receive message ";
        perror(msg.c_str());
    }

    std::cout << nodeType + ": Received message: " << this->buffer << std::endl;
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
    Message response = receiveAndParseMessage();
    if (response.keyword == RESPONSE)
    {
        std::map<std::string, fs::file_time_type> filesLastWrite;
        filesLastWrite = this->fileManager.getFilesLastWrite(DEFAULT_FOLDER_PATH);
        for (auto localFile : filesLastWrite)
        {
            if (response.args.find(localFile.first) != response.args.end())
            {
                if (localFile.second.time_since_epoch().count() > std::stoll(response.args[localFile.first]))
                {
                    // in this case server needs to update the file
                    this->updateFile(localFile.first);
                    this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), localFile.first.c_str());
                }
                else if (localFile.second.time_since_epoch().count() < std::stoll(response.args[localFile.first]))
                {
                    // in this case client needs to update the file
                    this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), localFile.first.c_str());
                }
                response.args.erase(localFile.first);
            }
            else
            {
                // in this case server needs to create the file
                this->createFile(localFile.first);
            }
        }
        if (response.args.size() > 0)
        {
            for (auto remoteFile : response.args)
            {
                // in this case client needs to delete the file
                this->readFile(remoteFile.first);
            }
        }
    }
    return "ACK";
}

void SquidProtocol::response(std::string ack)
{
    std::cout << "Sending response: " << ack << std::endl;
    this->sendMessage(this->formatter.responseFormat(ack));
}

void SquidProtocol::response(std::string nodeType, std::string processName)
{
    this->sendMessage(this->formatter.responseFormat(nodeType, processName));
}

void SquidProtocol::response(std::map<std::string, fs::file_time_type> filesLastWrite)
{
    this->sendMessage(this->formatter.responseFormat(filesLastWrite));
}

void SquidProtocol::response(bool lock)
{
    std::cout << "Sending response: " << lock << std::endl;
    this->sendMessage(this->formatter.responseFormat(lock));
}

void SquidProtocol::requestDispatcher(Message message)
{
    switch (message.keyword)
    {
    case CREATE_FILE:
        std::cout << nodeType + ": Dispatcher: create file\n";
        this->response(std::string("ACK"));
        std::cout << "calling file transfer.receiveFile" << std::endl;
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(std::string("ACK"));
        break;
    case TRANSFER_FILE:
        this->response(std::string("ACK"));
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(std::string("ACK"));
        break;
    case READ_FILE:
        this->response(std::string("ACK"));
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(std::string("ACK"));
        break;
    case UPDATE_FILE:
        this->response(std::string("ACK"));
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(std::string("ACK"));
        break;
    case DELETE_FILE:
        this->fileManager.deleteFile(message.args["filePath"]);
        this->response(std::string("ACK"));
        break;
    case ACQUIRE_LOCK:
        this->response(this->fileManager.acquireLock(message.args["filePath"]));
        break;
    case RELEASE_LOCK:
        this->fileManager.releaseLock(message.args["filePath"]);
        this->response(std::string("ACK"));
        break;
    case HEARTBEAT:
        this->response(std::string("ACK"));
        break;
    case SYNC_STATUS:
        this->response(this->fileManager.getFilesLastWrite(DEFAULT_FOLDER_PATH));
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