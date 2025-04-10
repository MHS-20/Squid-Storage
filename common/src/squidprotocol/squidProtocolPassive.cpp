#include "squidProtocolPassive.hpp"

SquidProtocolPassive::SquidProtocolPassive() {};

SquidProtocolPassive::SquidProtocolPassive(int socket_fd, std::string nodeType, std::string processName, SquidProtocolCommunicator communicator)
{
    this->socket_fd = socket_fd;
    this->processName = processName;
    this->nodeType = nodeType;
    this->communicator = communicator;

    this->fileTransfer = FileTransfer();
    this->formatter = SquidProtocolFormatter(nodeType);
}

void SquidProtocolPassive::response(std::string ack)
{
    std::cout << "Sending response: " << ack << std::endl;
    this->communicator.sendMessage(this->formatter.responseFormat(ack));
}

void SquidProtocolPassive::response(std::string nodeType, std::string processName)
{
    this->communicator.sendMessage(this->formatter.responseFormat(nodeType, processName));
}

void SquidProtocolPassive::response(std::map<std::string, fs::file_time_type> filesLastWrite)
{
    this->communicator.sendMessage(this->formatter.responseFormat(filesLastWrite));
}

void SquidProtocolPassive::response(bool lock)
{
    std::cout << "Sending response: " << lock << std::endl;
    this->communicator.sendMessage(this->formatter.responseFormat(lock));
}

void SquidProtocolPassive::responseIdentify()
{
    this->response(this->nodeType, this->processName);
    Message mex = communicator.receiveAndParseMessage();

    if (mex.args["ACK"] == "ACK")
        std::cout << nodeType + ": ACKed identify by server" << std::endl;
    else
    {
        std::cerr << nodeType + ": Error while identifying with server" << std::endl;
        return;
    }
}

// ------------------------------
// --------- DISPATCHER ---------
// ------------------------------
void SquidProtocolPassive::requestDispatcher(Message message)
{
    switch (message.keyword)
    {
    case CREATE_FILE:
        std::cout << nodeType + ": creating file\n";
        this->response(std::string("ACK"));
        std::cout << "receiving file" << std::endl;
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
        FileManager::getInstance().deleteFile(message.args["filePath"]);
        this->response(std::string("ACK"));
        break;
    case ACQUIRE_LOCK:
        this->response(FileManager::getInstance().acquireLock(message.args["filePath"]));
        break;
    case RELEASE_LOCK:
        FileManager::getInstance().releaseLock(message.args["filePath"]);
        this->response(std::string("ACK"));
        break;
    case HEARTBEAT:
        this->response(std::string("ACK"));
        break;
    case SYNC_STATUS:
        this->response(FileManager::getInstance().getFilesLastWrite(DEFAULT_FOLDER_PATH));
        break;
    case IDENTIFY:
        responseIdentify();
        break;
    case CLOSE:
        this->response(std::string("ACK"));
        close(this->socket_fd);
        socket_fd = -1;
        std::cout << nodeType + ": Connection closed" << std::endl;
        break;
    default:
        break;
    }
}
