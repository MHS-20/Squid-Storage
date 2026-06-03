#include "squidprotocol.hpp"
#include <utility>

class SquidProtocolServer : public SquidProtocol
{
public:
    int replicationFactor;
    std::map<std::string, SquidProtocolServer> *clientEndpointMap;
    std::map<std::string, SquidProtocolServer> *dataNodeEndpointMap;
    std::map<std::string, std::map<std::string, SquidProtocolServer>> dataNodeReplicationMap;

    std::map<std::string, SquidProtocolServer>::iterator endpointIterator;
    std::map<std::string, SquidProtocolServer>::iterator readsLoadBalancingIterator;

    SquidProtocolServer() : SquidProtocol()
    {
        this->replicationFactor = 0;
        this->clientEndpointMap = nullptr;
        this->dataNodeEndpointMap = nullptr;
        this->dataNodeReplicationMap = std::map<std::string, std::map<std::string, SquidProtocolServer>>();
    }

    SquidProtocolServer(FileManager &fileManager,
                        std::shared_ptr<INetworkChannel> channel, int replicationFactor,
                        std::string nodeType, std::string processName,
                        std::map<std::string, SquidProtocolServer> *clientEndpointMap,
                        std::map<std::string, SquidProtocolServer> *dataNodeEndpointMap)
    {
        this->fileManager_    = &fileManager;
        this->setChannel(std::move(channel));
        this->replicationFactor = replicationFactor;
        this->processName_    = processName;
        this->nodeType_       = nodeType;
        this->alive_          = true;
        this->clientEndpointMap   = clientEndpointMap;
        this->dataNodeEndpointMap = dataNodeEndpointMap;
        this->dataNodeReplicationMap = std::map<std::string, std::map<std::string, SquidProtocolServer>>();
        this->endpointIterator = dataNodeEndpointMap->begin();
    }

    void createFileReplication(const std::string &filePath)
    {
        auto fileHoldersMap = std::map<std::string, SquidProtocolServer>();
        for (int i = 0; i < replicationFactor; i++)
        {
            if (endpointIterator == dataNodeEndpointMap->end())
                endpointIterator = dataNodeEndpointMap->begin();
            fileHoldersMap.insert({endpointIterator->first, endpointIterator->second});
            endpointIterator++;
        }
        dataNodeReplicationMap.insert({filePath, fileHoldersMap});
        this->readsLoadBalancingIterator = dataNodeReplicationMap[filePath].begin();
    }

    void getFileFromDataNode(const std::string &filePath)
    {
        auto &fileHoldersMap = dataNodeReplicationMap[filePath];
        if (readsLoadBalancingIterator == fileHoldersMap.end())
            readsLoadBalancingIterator = fileHoldersMap.begin();
        this->responseDispatcher(readsLoadBalancingIterator->second.readFile(filePath));
        readsLoadBalancingIterator++;
    }

    void requestDispatcher(const Message &message) override
    {
        std::string path = message.getString(FieldID::FILE_PATH);
        int version      = static_cast<int>(message.getUint32(FieldID::FILE_VERSION, 0));

        switch (message.opcode)
        {
        case Opcode::CREATE_FILE:
            response(true);
            if (fileTransfer_.receiveFile(*channel_, processName_, path))
            {
                (*fileManager_).setFileVersion(path, version);
                response(true);
            }
            else
            {
                response(false);
            }
            for (auto &client : *clientEndpointMap)
                client.second.createFile(path);
            createFileReplication(path);
            for (auto &datanode : dataNodeReplicationMap[path])
                datanode.second.createFile(path);
            break;

        case Opcode::READ_FILE:
            response(true);
            getFileFromDataNode(path);
            response(fileTransfer_.sendFile(*channel_, processName_, path));
            break;

        case Opcode::UPDATE_FILE:
            response(true);
            if (fileTransfer_.receiveFile(*channel_, processName_, path))
            {
                (*fileManager_).setFileVersion(path, version);
                response(true);
            }
            else
            {
                response(false);
            }
            for (auto &client : *clientEndpointMap)
                client.second.updateFile(path);
            for (auto &datanode : dataNodeReplicationMap[path])
                datanode.second.updateFile(path);
            break;

        case Opcode::DELETE_FILE:
            (*fileManager_).deleteFile(path);
            response(true);
            for (auto &client : *clientEndpointMap)
                client.second.deleteFile(path);
            for (auto &datanode : dataNodeReplicationMap[path])
                datanode.second.deleteFile(path);
            dataNodeReplicationMap.erase(path);
            break;

        case Opcode::ACQUIRE_LOCK:
            response((*fileManager_).acquireLock(path));
            break;

        case Opcode::RELEASE_LOCK:
            (*fileManager_).releaseLock(path);
            response(true);
            break;

        case Opcode::HEARTBEAT:
            response(true);
            break;

        case Opcode::SYNC_STATUS:
            response((*fileManager_).getFileVersionMap(FileManager::storageRoot().string()));
            break;

        case Opcode::CLOSE:
            response(true);
            if (channel_) channel_->close();
            alive_ = false;
            std::cout << nodeType_ + ": Connection closed" << std::endl;
            break;

        default:
            break;
        }
    }
};
