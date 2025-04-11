#include "squidProtocolPassive.hpp"

class SquidProtocolPassiveServer : public SquidProtocolPassive
{
public:
    SquidProtocolPassiveServer() : SquidProtocolPassive() {}
    SquidProtocolPassiveServer(int socket_fd, std::string nodeType, std::string processName, SquidProtocolCommunicator communicator) : SquidProtocolPassive(socket_fd, nodeType, processName, communicator) {}

    void requestDispatcher(Message message) override
    {
        switch (message.keyword)
        {
        case CREATE_FILE:
            std::cout << nodeType + ": creating file\n";
            this->response(std::string("ACK"));
            std::cout << "receiving file" << std::endl;
            this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
            this->response(std::string("ACK"));

            // ! propagate updates to client
            // ! replicate on datanode

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

            // ! propagate updates

            break;
        case DELETE_FILE:
            FileManager::getInstance().deleteFile(message.args["filePath"]);
            this->response(std::string("ACK"));

            // ! propagate updates

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
};
