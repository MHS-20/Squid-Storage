#include "squidProtocolActive.hpp"

SquidProtocolActive::SquidProtocolActive(){};
SquidProtocolActive::SquidProtocolActive(int socket_fd, std::string nodeType, std::string processName, SquidProtocolCommunicator communicator){
    this->socket_fd = socket_fd;
    this->processName = processName;
    this->nodeType = nodeType;
    this->communicator = communicator;
    this->fileTransfer = FileTransfer();
    this->formatter = SquidProtocolFormatter(nodeType);
}

// ----------------------------
// --------- REQUESTS ---------
// ----------------------------

Message SquidProtocolActive::closeConn()
{
    this->communicator.sendMessage(this->formatter.closeFormat());
    return this->communicator.receiveAndParseMessage();
}

Message SquidProtocolActive::identify()
{
    this->communicator.sendMessage(this->formatter.identifyFormat());
    return this->communicator.receiveAndParseMessage();
}

Message SquidProtocolActive::createFile(std::string filePath)
{
    std::cout << nodeType + ": sending create file request" << std::endl;
    this->communicator.sendMessage(this->formatter.createFileFormat(filePath));
    // std::cout << nodeType + ": sent create file request" << std::endl;
    Message response = this->communicator.receiveAndParseMessage();
    std::cout << nodeType + ": received create file response" << std::endl;
    communicator.transferFile(filePath, response);
    return communicator.receiveAndParseMessage();
}

Message SquidProtocolActive::updateFile(std::string filePath)
{
    this->communicator.sendMessage(this->formatter.updateFileFormat(filePath));
    Message response = communicator.receiveAndParseMessage();
    communicator.transferFile(filePath, response);
    return communicator.receiveAndParseMessage();
}

Message SquidProtocolActive::readFile(std::string filePath)
{
    this->communicator.sendMessage(this->formatter.readFileFormat(filePath));
    Message response = communicator.receiveAndParseMessage();
    if (response.keyword == RESPONSE && response.args["ACK"] == "ACK")
        this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), filePath.c_str());
    return communicator.receiveAndParseMessage();
}

Message SquidProtocolActive::deleteFile(std::string filePath)
{
    this->communicator.sendMessage(this->formatter.deleteFileFormat(filePath));
    return communicator.receiveAndParseMessage();
}

Message SquidProtocolActive::acquireLock(std::string filePath)
{
    this->communicator.sendMessage(this->formatter.acquireLockFormat(filePath));
    return communicator.receiveAndParseMessage();
}

Message SquidProtocolActive::releaseLock(std::string filePath)
{
    this->communicator.sendMessage(this->formatter.releaseLockFormat(filePath));
    return communicator.receiveAndParseMessage();
}

Message SquidProtocolActive::heartbeat()
{
    this->communicator.sendMessage(this->formatter.heartbeatFormat());
    return communicator.receiveAndParseMessage();
}

Message SquidProtocolActive::syncStatus()
{
    this->communicator.sendMessage(this->formatter.syncStatusFormat());
    Message response = communicator.receiveAndParseMessage();
    if (response.keyword == RESPONSE)
    {
        std::map<std::string, fs::file_time_type> filesLastWrite;
        filesLastWrite = FileManager::getInstance().getFilesLastWrite(DEFAULT_FOLDER_PATH);
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
    // return "ACK";
    return formatter.parseMessage(formatter.responseFormat("ACK"));
}

void SquidProtocolActive::responseDispatcher(Message response)
{
    switch (response.keyword)
    {
    case CREATE_FILE:
        if (response.args["ACK"] != "ACK")
            perror(std::string(nodeType + ": Error while creating file: " + response.args["filePath"]).c_str());
        else
            std::cout << nodeType + ": Created file successfully on server" << std::endl;
        break;
    case TRANSFER_FILE:
        if (response.args["ACK"] != "ACK")
            perror(std::string(nodeType + ": Error while transfering file: " + response.args["filePath"]).c_str());
        else
            std::cout << nodeType + ": Transfered file successfully on server" << std::endl;
        break;
    case READ_FILE:
        if (response.args["ACK"] != "ACK")
            perror(std::string(nodeType + ": Error while reading file: " + response.args["filePath"]).c_str());
        else
            std::cout << nodeType + ": Read file successfully on server" << std::endl;
        break;
    case UPDATE_FILE:
        if (response.args["ACK"] != "ACK")
            perror(std::string(nodeType + ": Error while updating file: " + response.args["filePath"]).c_str());
        else
            std::cout << nodeType + ": Updated file successfully on server" << std::endl;
        break;
    case DELETE_FILE:
        if (response.args["ACK"] != "ACK")
            perror(std::string(nodeType + ": Error while deleting file: " + response.args["filePath"]).c_str());
        else
            std::cout << nodeType + ": Deleted file successfully on server" << std::endl;
        break;
    case ACQUIRE_LOCK:
        if (response.args["LOCK"] != "true")
            perror(std::string(nodeType + ": Lock refused for file: " + response.args["filePath"]).c_str());
        else
            std::cout << nodeType + ": Acquired lock successfully on server" << std::endl;
        break;
    case RELEASE_LOCK:
        if (response.args["ACK"] != "ACK")
            perror(std::string(nodeType + ": Error while releasing lock for file: " + response.args["filePath"]).c_str());
        else
            std::cout << nodeType + ": Released lock successfully on server" << std::endl;
        break;
    case HEARTBEAT:
        if (response.args["ACK"] != "ACK")
            perror(std::string(nodeType + ": Heartbeat error").c_str());
        else
            std::cout << nodeType + ": Received heartbeat successfully from server" << std::endl;
        break;
    case SYNC_STATUS:
        if (response.args["ACK"] != "ACK")
            perror(std::string(nodeType + ": Error while synchronizing state").c_str());
        else
            std::cout << nodeType + ": Synchronization with server successful" << std::endl;
        break;
    // case IDENTIFY:
    //     if (response.args["ACK"] != "ACK")
    //         perror(std::string(nodeType + ": Error while identifying").c_str());
    //     else
    //         std::cout << nodeType + ": Identified successfully on server" << std::endl;
    //     break;
    case CLOSE:
        if (response.args["ACK"] != "ACK")
            perror(std::string(nodeType + ": Error while closing connection").c_str());
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