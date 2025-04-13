#include "squidprotocol.hpp"

class SquidProtocolServer : public SquidProtocol
{

public:
    int replicationFactor;
    std::map<std::string, SquidProtocolServer> clientEndpointMap;
    std::map<std::string, SquidProtocolServer> dataNodeEndpointMap;
    std::map<std::string, std::map<std::string, SquidProtocolServer>> dataNodeReplicationMap;
    std::map<std::string, SquidProtocolServer>::iterator endpointIterator;

    SquidProtocolServer() : SquidProtocol() {}
    SquidProtocolServer(int socket_fd, int replicationFactor,
                        std::string nodeType, std::string processName,
                        std::map<std::string, SquidProtocolServer> &clientEndpointMap,
                        std::map<std::string, SquidProtocolServer> &dataNodeEndpointMap,
                        std::map<std::string, std::map<std::string, SquidProtocolServer>> &dataNodeReplicationMap)
    {
        this->socket_fd = socket_fd;
        this->replicationFactor = replicationFactor;
        this->processName = processName;
        this->nodeType = nodeType;

        this->fileTransfer = FileTransfer();
        this->formatter = SquidProtocolFormatter(nodeType);

        this->clientEndpointMap = clientEndpointMap;
        this->dataNodeEndpointMap = dataNodeEndpointMap;
        this->dataNodeReplicationMap = dataNodeReplicationMap;
        auto endpointIterator = dataNodeEndpointMap.begin();
    }

    void createFileReplication(std::string filePath)
    { // round robin replication
        auto fileHoldersMap = std::map<std::string, SquidProtocolServer>();
        for (int i = 0; i < replicationFactor; i++)
        {
            if (endpointIterator == dataNodeEndpointMap.end())
                endpointIterator = dataNodeEndpointMap.begin();
            fileHoldersMap.insert({endpointIterator->first, endpointIterator->second});
            endpointIterator++;
        }
        dataNodeReplicationMap.insert({filePath, fileHoldersMap});
    }

    void requestDispatcher(Message message) override
    {
        switch (message.keyword)
        {
        case CREATE_FILE:
            this->response(std::string("ACK"));
            this->fileTransfer.receiveFile(this->socket_fd, this->processName.c_str(), message.args["filePath"].c_str());
            this->response(std::string("ACK"));
            for (auto &client : clientEndpointMap)
                client.second.createFile(message.args["filePath"]);
            createFileReplication(message.args["filePath"]);
            for (auto &datanode : dataNodeReplicationMap[message.args["filePath"]])
                datanode.second.createFile(message.args["filePath"]);
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
            for (auto &client : clientEndpointMap)
                client.second.updateFile(message.args["filePath"]);
            for (auto &datanode : dataNodeReplicationMap[message.args["filePath"]])
                datanode.second.updateFile(message.args["filePath"]);
            break;
        case DELETE_FILE:
            FileManager::getInstance().deleteFile(message.args["filePath"]);
            this->response(std::string("ACK"));
            for (auto &client : clientEndpointMap)
                client.second.deleteFile(message.args["filePath"]);
            for (auto &datanode : dataNodeReplicationMap[message.args["filePath"]])
                datanode.second.deleteFile(message.args["filePath"]);
            dataNodeReplicationMap.erase(message.args["filePath"]);
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