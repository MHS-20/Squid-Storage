#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <memory>

#include "filetransfer.hpp"
#include "filemanager.hpp"
#include "INetworkChannel.hpp"
#include "squidProtocolFormatter.hpp"

namespace fs = std::filesystem;

// Maximum allowed payload length for a single frame (16 MiB).
// Prevents remote-triggered OOM from a crafted oversized payloadLen.
static constexpr uint32_t MAX_PAYLOAD_SIZE = 16 * 1024 * 1024;

class SquidProtocol
{
public:
    SquidProtocol();
    SquidProtocol(FileManager &fileManager, int socket_fd, std::string nodeType, std::string processName);
    SquidProtocol(FileManager &fileManager, std::shared_ptr<INetworkChannel> channel, std::string nodeType, std::string processName);
    virtual ~SquidProtocol();

    virtual bool        isAlive()        const { return alive_.load(std::memory_order_acquire); }
    virtual void        setIsAlive(bool v)     { alive_.store(v, std::memory_order_release); }
    virtual void        setSocket(int fd);
    virtual void        setChannel(std::shared_ptr<INetworkChannel> channel);
    virtual std::string getProcessName() const { return processName_; }
    virtual std::string getNodeType()    const { return nodeType_; }
    virtual std::string toString()       const;
    virtual int         socketFd()       const { return channel_ ? channel_->getSocket() : -1; }

    // Sequence number of the last message delivered to the caller.
    // Useful for diagnostics and for matching responses to requests.
    uint32_t getLastSeq() const { return lastDeliveredSeq_; }

    void sendFrame(std::vector<uint8_t> frame);
    std::vector<uint8_t> receiveFrame();
    virtual Message receiveAndParse();

    virtual Message identify();
    virtual Message connectServer();
    virtual Message closeConn();

    // syncStatus() is the single operation for querying the file version map,
    // used both server→datanode and client→server. listFiles() has been removed.
    virtual Message syncStatus();

    virtual Message createFile(const std::string &filePath);
    virtual Message createFile(const std::string &filePath, int version);
    virtual Message createFile(const std::string &filePath, int version, const std::vector<uint8_t> &fileData);
    virtual Message readFile  (const std::string &filePath, std::vector<uint8_t> &fileData);
    virtual Message updateFile(const std::string &filePath);
    virtual Message updateFile(const std::string &filePath, int version);
    virtual Message updateFile(const std::string &filePath, int version, const std::vector<uint8_t> &fileData);
    virtual Message deleteFile(const std::string &filePath);

    bool receiveFileData(std::vector<uint8_t> &fileData);
    bool sendFileData(const std::vector<uint8_t> &fileData);

    // Push helpers: send a dedicated PUSH_* opcode frame + file bytes with no
    // ACK handshake. The receiver distinguishes pushes from request/responses
    // by opcode, so there is no ambiguity even if a client request is in-flight.
    void pushCreateFile(const std::string &filePath, int version,
                        const std::vector<uint8_t> &fileData);
    void pushUpdateFile(const std::string &filePath, int version,
                        const std::vector<uint8_t> &fileData);
    void pushDeleteFile(const std::string &filePath);

    // ── Standby-replication helpers (primary → standby) ───────────────────
    // Send a full state snapshot to a newly connected standby.
    void sendStateSnap(const std::map<std::string, int> &versionMap,
                       const std::map<std::string, std::set<std::string>> &repMap,
                       uint32_t epoch);

    // Send an incremental delta after a committed write.
    // op: 0=CREATE/UPDATE, 1=DELETE
    void sendStateDelta(uint8_t op, const std::string &filePath, int version,
                        const std::vector<std::string> &datanodes, uint32_t epoch);

    // Send a leader heartbeat carrying the current epoch.
    void sendLeaderHb(uint32_t epoch);

    // Send epoch-fencing rejection (datanode/client → stale server).
    void sendNackStaleEpoch(uint32_t myEpoch);

    virtual Message acquireLock(const std::string &filePath);
    virtual Message releaseLock(const std::string &filePath);
    virtual Message heartbeat();

    virtual void response(bool isAck);
    virtual void response(bool isAck, int version);  // ACK + FILE_VERSION in one frame
    virtual void response(int port);
    virtual void response(const std::string &ack);
    virtual void response(const std::string &nodeType, const std::string &processName);
    virtual void response(const std::map<std::string, int> &fileVersionMap);
    virtual void response(const std::map<std::string, long long> &fileTimeMap);
    virtual void response(const std::map<std::string, fs::file_time_type> &filesLastWrite);

    virtual void requestDispatcher (const Message &request);
    virtual void responseDispatcher(const Message &response);

protected:
    std::atomic<bool>   alive_{false};
    std::string processName_;
    std::string nodeType_;
    std::shared_ptr<INetworkChannel> channel_;

    FileManager           *fileManager_ = nullptr;
    FileTransfer           fileTransfer_;
    SquidProtocolFormatter formatter_;

    // ── Sequence-number state ────────────────────────────────────────────────
    // Outbound: monotonically increasing counter stamped into every frame sent.
    uint32_t nextSendSeq_     = 0;

    // Inbound: the seq we expect to deliver next to the caller.
    uint32_t expectedRecvSeq_ = 0;

    // Last seq actually handed to the caller (exposed via getLastSeq()).
    uint32_t lastDeliveredSeq_ = 0;

    // Out-of-order buffer: frames that arrived ahead of expectedRecvSeq_.
    // Keyed by seq so we can cheaply find the next one when a gap closes.
    std::map<uint32_t, std::vector<uint8_t>> reorderBuffer_;
    // ────────────────────────────────────────────────────────────────────────

    bool recvExact(uint8_t *buf, size_t n);
    bool handleRecvError(ssize_t bytes);
    bool waitForAck(Message &ackMsg, const std::string &operation);
    Message waitForTransferResult(const std::string &operation);
    bool sendFileAfterAck(const std::string &filePath, const Message &ackMsg);
    bool sendFileAfterAck(const std::vector<uint8_t> &fileData, const Message &ackMsg);
    bool receiveFileAfterAck(std::vector<uint8_t> &fileData, const Message &ackMsg);

private:
    // Try to pop the next expected frame from the reorder buffer.
    // Returns true and fills `frame` if expectedRecvSeq_ is available.
    bool popNextBuffered(std::vector<uint8_t> &frame);
};
