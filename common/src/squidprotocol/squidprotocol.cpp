#include "squidprotocol.hpp"

SquidProtocol::SquidProtocol() {}

SquidProtocol::SquidProtocol(int socket_fd, std::string nodeType, std::string processName)
{
    this->socket_fd = socket_fd;
    this->processName = processName;
    this->nodeType = nodeType;
    this->alive = true;

    this->fileTransfer = FileTransfer();
    this->formatter = SquidProtocolFormatter(nodeType);
}

SquidProtocol::~SquidProtocol() {}

bool SquidProtocol::isAlive()
{
    return alive;
}

int SquidProtocol::getSocket()
{
    return socket_fd;
}

std::string SquidProtocol::toString() const
{
    return "Protocol{" + nodeType + ":" + processName + "}";
}

Message SquidProtocol::closeConn()
{
    this->sendMessage(this->formatter.closeFormat());
    return receiveAndParseMessage();
}

Message SquidProtocol::identify()
{
    this->sendMessage(this->formatter.identifyFormat());
    return this->receiveAndParseMessage();
}

// ----------------------------
// --------- REQUESTS ---------
// ----------------------------

Message SquidProtocol::createFile(std::string filePath)
{
    std::cout << nodeType + ": sending create file request" << std::endl;
    std::cout << "file name: " + filePath << std::endl;
    this->sendMessage(this->formatter.createFileFormat(filePath));
    // std::cout << nodeType + ": sent create file request" << std::endl;
    Message response = receiveAndParseMessage();
    std::cout << nodeType + ": received create file response" << std::endl;
    transferFile(filePath, response);
    return receiveAndParseMessage();
}

Message SquidProtocol::updateFile(std::string filePath)
{
    this->sendMessage(this->formatter.updateFileFormat(filePath));
    Message response = receiveAndParseMessage();
    transferFile(filePath, response);
    return receiveAndParseMessage();
}

void SquidProtocol::transferFile(std::string filePath, Message response)
{
    std::cout << nodeType + ": trying transfering file" << std::endl;
    std::cout << response.toString() << std::endl;

    if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
    else
        std::cerr << nodeType + ": Error while transfering file: " + filePath << std::endl;
    return;
}

Message SquidProtocol::readFile(std::string filePath)
{
    std::cout << nodeType + ": sending read file request" << std::endl;
    this->sendMessage(this->formatter.readFileFormat(filePath));
    Message response = receiveAndParseMessage();
    if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
    return receiveAndParseMessage();
}

Message SquidProtocol::deleteFile(std::string filePath)
{
    this->sendMessage(this->formatter.deleteFileFormat(filePath));
    return receiveAndParseMessage();
}

Message SquidProtocol::acquireLock(std::string filePath)
{
    std::cout << nodeType + ": sending acquire lock request for " << filePath << std::endl;
    this->sendMessage(this->formatter.acquireLockFormat(filePath));
    return receiveAndParseMessage();
}

Message SquidProtocol::releaseLock(std::string filePath)
{
    this->sendMessage(this->formatter.releaseLockFormat(filePath));
    return receiveAndParseMessage();
}

Message SquidProtocol::heartbeat()
{
    this->sendMessage(this->formatter.heartbeatFormat());
    return receiveAndParseMessage();
}

// executed by server
Message SquidProtocol::listFiles()
{
    std::cout << nodeType + ": sending list files request" << std::endl;
    this->sendMessage(this->formatter.syncStatusFormat());
    Message response = receiveAndParseMessage();
    std::cout << nodeType + ": received list files response" << std::endl;
    return response;
}

Message SquidProtocol::connectServer()
{
    std::cout << nodeType + ": sending connect server request" << std::endl;
    this->sendMessage(this->formatter.connectServerFormat());
    Message response = receiveAndParseMessage();
    std::cout << nodeType + ": received connect server response" << std::endl;
    return response;
}

// executed by client
Message SquidProtocol::syncStatus()
{
    std::cout << nodeType + ": sending sync status request" << std::endl;
    this->sendMessage(this->formatter.syncStatusFormat());
    Message response = receiveAndParseMessage();
    if (response.keyword == RESPONSE)
    {
        std::cout << nodeType + ": received sync status response" << std::endl;
        std::map<std::string, fs::file_time_type> filesLastWrite;
        filesLastWrite = FileManager::getInstance().getFilesLastWrite(DEFAULT_FOLDER_PATH);
        std::cout << nodeType + ": checking files: " << std::endl;
        for (auto localFile : filesLastWrite)
        {
            if (response.args.find(localFile.first) != response.args.end())
            {
                std::cout << nodeType + ": found file that already exists on server " << localFile.first << std::endl;
                if (localFile.second.time_since_epoch().count() > std::stoll(response.args[localFile.first]))
                {
                    // in this case server needs to update the file
                    std::cout << nodeType + ": server needs to update the file: " + localFile.first << std::endl;
                    this->updateFile(localFile.first);
                    // this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), localFile.first.c_str());
                }
                else if (localFile.second.time_since_epoch().count() < std::stoll(response.args[localFile.first]))
                {
                    // in this case client needs to update the file
                    // this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), localFile.first.c_str());
                    std::cout << nodeType + ": client needs to update the file: " + localFile.first << std::endl;
                    this->readFile(localFile.first);
                }
                response.args.erase(localFile.first);
            }
            else
            {
                // in this case server needs to create the file
                std::cout << nodeType + ": server needs to create the file: " + localFile.first << std::endl;
                this->createFile(localFile.first);
            }
        }
        if (response.args.size() > 0)
        {
            for (auto remoteFile : response.args)
            {
                // in this case client needs to create the file
                // this->fileManager.deleteFile(remoteFile.first);
                std::cout << nodeType + ": client needs to create the file: " + remoteFile.first << std::endl;
                this->readFile(remoteFile.first);
            }
        }
    }
    // return "ACK";
    return formatter.parseMessage(formatter.responseFormat(std::string("ACK")));
}

// ---------------------------
// --------- PARSING ---------
// ---------------------------
Message SquidProtocol::receiveAndParseMessage()
{
    std::string receivedMessage = receiveMessageWithLength();
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
        std::cerr << std::string(nodeType) + ": Failed to receive message";
        //throw std::runtime_error("Failed to receive message");
    }
}

std::string SquidProtocol::receiveMessageWithLength()
{
    // Read the length of the message
    uint32_t messageLength;
    ssize_t bytesRead = recv(socket_fd, &messageLength, sizeof(messageLength), 0);
    checkBytesRead(bytesRead, nodeType);

    messageLength = ntohl(messageLength);
    // std::cout << std::this_thread::get_id();
    std::cout << nodeType + ": Expecting message of length: " << messageLength << std::endl;

    // Read the actual message
    char *buffer = new char[messageLength + 1];
    bytesRead = recv(socket_fd, buffer, messageLength, 0);
    checkBytesRead(bytesRead, nodeType);

    buffer[messageLength] = '\0';
    std::string message(buffer);
    delete[] buffer;

   // std::cout << std::this_thread::get_id();
    std::cout << "[INFO]: Received message: " << message << std::endl;
    return message;
}

// -----------------------------
// --------- RESPONSES ---------
// -----------------------------

void SquidProtocol::response(std::string ack)
{
    std::cout << nodeType + "Sending response: " << ack << std::endl;
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

void SquidProtocol::response(std::map<std::string, long long> fileTimeMap){
    this->sendMessage(this->formatter.responseFormat(fileTimeMap));
}

void SquidProtocol::response(bool lock)
{
    std::cout << nodeType + "Sending response: " << lock << std::endl;
    this->sendMessage(this->formatter.responseFormat(lock));
}

// --------------------------------
// --------- SEND MESSAGE ---------
// --------------------------------

void SquidProtocol::sendMessage(std::string message)
{
    sendMessageWithLength(message);
}

void SquidProtocol::sendMessageWithLength(std::string &message)
{
    uint32_t messageLength = htonl(message.size());
    if (socket_fd < 0)
    {
        std::cerr << nodeType + "[ERROR]: Invalid socket_fd" << std::endl;
        return;
    }
    
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

    std::cout << nodeType + ": Sent message with length: " << message.size() << std::endl;
}

// -------------------------------
// --------- DISPATCHERS ---------
// -------------------------------
void SquidProtocol::requestDispatcher(Message message)
{
    switch (message.keyword)
    {
    case CREATE_FILE:
        std::cout << nodeType + ": received create file request\n";
        this->response(std::string("ACK"));
        std::cout << nodeType + ": Receiving file" << std::endl;
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(std::string("ACK"));
        break;
    case TRANSFER_FILE:
        this->response(std::string("ACK"));
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(std::string("ACK"));
        break;
    case READ_FILE:
        std::cout << nodeType + ": received read file request\n";
        this->response(std::string("ACK"));
        this->fileTransfer.sendFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(std::string("ACK"));
        break;
    case UPDATE_FILE:
        std::cout << nodeType + ": received update file request\n";
        this->response(std::string("ACK"));
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
        this->response(std::string("ACK"));
        break;
    case DELETE_FILE:
        std::cout << nodeType + ": received delete file request\n";
        FileManager::getInstance().deleteFile(message.args["filePath"]);
        this->response(std::string("ACK"));
        break;
    // case ACQUIRE_LOCK:
    //     std::cout << nodeType + ": received acquire lock request for " << message.args["filePath"] << std::endl;
    //     this->response(FileManager::getInstance().acquireLock(message.args["filePath"]));
    //     break;
    // case RELEASE_LOCK:
    //     FileManager::getInstance().releaseLock(message.args["filePath"]);
    //     this->response(std::string("ACK"));
    //     break;
    case HEARTBEAT:
        this->response(std::string("ACK"));
        break;
    case SYNC_STATUS:
        std::cout << nodeType + ": received sync status request\n";
        this->response(FileManager::getInstance().getFilesLastWrite(DEFAULT_FOLDER_PATH));
        break;
    case IDENTIFY:
        this->response(this->nodeType, this->processName);
        break;
    case CLOSE:
        this->response(std::string("ACK"));
        close(this->socket_fd);
        socket_fd = -1;
        std::cout << nodeType + ": Connection closed" << std::endl;
        break;
    default:
        std::cerr << nodeType + ": Unknown request: " + message.toString() << std::endl;
        break;
    }
}

void SquidProtocol::responseDispatcher(Message response)
{
    switch (response.keyword)
    {
    case RESPONSE:
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error from server " + response.toString() << std::endl;
        else
            std::cout << nodeType + ": Operation performed" << std::endl;
        break;
    case CREATE_FILE:
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error while creating file: " + response.args["filePath"] << std::endl;
        else
            std::cout << nodeType + ": Created file successfully on server" << std::endl;
        break;
    case TRANSFER_FILE:
        std::cout << "tf resp: " + response.args["ACK"] << std::endl;
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error while transfering file: " + response.args["ACK"] << std::endl;
        else
            std::cout << nodeType + ": Transfered file successfully on server" << std::endl;
        break;
    case READ_FILE:
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error while reading file: " + response.args["filePath"] << std::endl;
        else
            std::cout << nodeType + ": Read file successfully on server" << std::endl;
        break;
    case UPDATE_FILE:
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error while updating file: " + response.args["filePath"] << std::endl;
        else
            std::cout << nodeType + ": Updated file successfully on server" << std::endl;
        break;
    case DELETE_FILE:
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error while deleting file: " + response.args["filePath"] << std::endl;
        else
            std::cout << nodeType + ": Deleted file successfully on server" << std::endl;
        break;
    case ACQUIRE_LOCK:
        if (response.args["LOCK"] != "1")
            std::cerr << nodeType + ": Lock refused for file: " + response.args["filePath"] << std::endl;
        else
            std::cout << nodeType + ": Acquired lock successfully on server" << std::endl;
        break;
    case RELEASE_LOCK:
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error while releasing lock for file: " + response.args["filePath"] << std::endl;
        else
            std::cout << nodeType + ": Released lock successfully on server" << std::endl;
        break;
    case HEARTBEAT:
        if (response.args["ACK"] != "ACK")
        {
            alive = false;
            std::cerr << nodeType + ": Heartbeat error" << std::endl;
        }
        else
        {
            alive = true;
            std::cout << nodeType + ": Received heartbeat successfully from server" << std::endl;
        }
        break;
    case SYNC_STATUS:
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error while synchronizing state" << std::endl;
        else
            std::cout << nodeType + ": Synchronization with server successful" << std::endl;
        break;
    case IDENTIFY:
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error while identifying" << std::endl;
        else
            std::cout << nodeType + ": Identified successfully on server" << std::endl;
        break;
    case CLOSE:
        if (response.args["ACK"] != "ACK")
            std::cerr << nodeType + ": Error while closing connection" << std::endl;
        else
        {
            close(this->socket_fd);
            socket_fd = -1;
            std::cout << nodeType + ": Connection closed successfully" << std::endl;
        }
        break;
    default:
        break;
    }
}