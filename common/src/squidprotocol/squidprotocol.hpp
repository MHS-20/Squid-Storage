#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <cstdint>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <memory>

#include "filetransfer.hpp"
#include "filemanager.hpp"
#include "INetworkChannel.hpp"
#include "squidProtocolFormatter.hpp"

namespace fs = std::filesystem;

class SquidProtocol
{
public:
    SquidProtocol();
    SquidProtocol(FileManager &fileManager, int socket_fd, std::string nodeType, std::string processName);
    SquidProtocol(FileManager &fileManager, std::shared_ptr<INetworkChannel> channel, std::string nodeType, std::string processName);
    virtual ~SquidProtocol();

    virtual bool        isAlive()        const { return alive_; }
    virtual void        setIsAlive(bool v)     { alive_ = v; }
    virtual void        setSocket(int fd);
    virtual void        setChannel(std::shared_ptr<INetworkChannel> channel);
    virtual std::string getProcessName() const { return processName_; }
    virtual std::string getNodeType()    const { return nodeType_; }
    virtual std::string toString()       const;
    virtual int         socketFd()       const { return channel_ ? channel_->getSocket() : -1; }

    void sendFrame(const std::vector<uint8_t> &frame);
    std::vector<uint8_t> receiveFrame();
    virtual Message receiveAndParse();

    virtual Message identify();
    virtual Message connectServer();
    virtual Message closeConn();
    virtual Message listFiles();
    virtual Message syncStatus();

    virtual Message createFile(const std::string &filePath);
    virtual Message createFile(const std::string &filePath, int version);
    virtual Message createFile(const std::string &filePath, int version, const std::vector<uint8_t> &fileData);
    virtual Message readFile  (const std::string &filePath);
    virtual Message readFile  (const std::string &filePath, std::vector<uint8_t> &fileData);
    virtual Message updateFile(const std::string &filePath);
    virtual Message updateFile(const std::string &filePath, int version);
    virtual Message updateFile(const std::string &filePath, int version, const std::vector<uint8_t> &fileData);
    virtual Message deleteFile(const std::string &filePath);

    bool receiveFileData(std::vector<uint8_t> &fileData);
    bool sendFileData(const std::vector<uint8_t> &fileData);

    // Push helpers: send a header frame + file bytes with no ACK handshake.
    // Use these when the server pushes a file to a passive receiver (client),
    // as opposed to the request/response overloads above which expect ACKs.
    void pushCreateFile(const std::string &filePath, int version,
                        const std::vector<uint8_t> &fileData);
    void pushUpdateFile(const std::string &filePath, int version,
                        const std::vector<uint8_t> &fileData);

    virtual Message acquireLock(const std::string &filePath);
    virtual Message releaseLock(const std::string &filePath);
    virtual Message heartbeat();

    virtual void response(bool isAck);
    virtual void response(int port);
    virtual void response(const std::string &ack);
    virtual void response(const std::string &nodeType, const std::string &processName);
    virtual void response(const std::map<std::string, int> &fileVersionMap);
    virtual void response(const std::map<std::string, long long> &fileTimeMap);
    virtual void response(const std::map<std::string, fs::file_time_type> &filesLastWrite);

    virtual void requestDispatcher (const Message &request);
    virtual void responseDispatcher(const Message &response);

protected:
    bool        alive_       = false;
    std::string processName_;
    std::string nodeType_;
    std::shared_ptr<INetworkChannel> channel_;

    FileManager           *fileManager_ = nullptr;
    FileTransfer           fileTransfer_;
    SquidProtocolFormatter formatter_;

    bool recvExact(uint8_t *buf, size_t n);
    bool handleRecvError(ssize_t bytes);
    bool waitForAck(Message &ackMsg, const std::string &operation);
    Message waitForTransferResult(const std::string &operation);
    bool sendFileAfterAck(const std::string &filePath, const Message &ackMsg);
    bool sendFileAfterAck(const std::vector<uint8_t> &fileData, const Message &ackMsg);
    bool receiveFileAfterAck(std::vector<uint8_t> &fileData, const Message &ackMsg);
};
