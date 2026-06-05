#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <cstdint>
#include <stdexcept>

namespace fs = std::filesystem;

static constexpr uint16_t SQUID_MAGIC   = 0x5351;
static constexpr uint8_t  FLAG_RESPONSE = 0x01;
// Header layout (13 bytes):
//   [magic:2][opcode:1][flags:1][nfields:1][seq:4][payloadLen:4]
static constexpr size_t   FRAME_HEADER_SIZE = 13;

enum class Opcode : uint8_t
{
    CONNECT_SERVER = 0x01,
    IDENTIFY      = 0x02,
    CLOSE         = 0x03,
    HEARTBEAT     = 0x04,
    SYNC_STATUS   = 0x05,
    CREATE_FILE   = 0x10,
    READ_FILE     = 0x12,
    UPDATE_FILE   = 0x13,
    DELETE_FILE   = 0x14,
    ACQUIRE_LOCK  = 0x20,
    RELEASE_LOCK  = 0x21,
    // Server→client unsolicited push opcodes. Using dedicated values means the
    // receiver can distinguish a push from a request/response frame by opcode
    // alone, with no need for correlation IDs.
    PUSH_CREATE_FILE = 0x30,
    PUSH_UPDATE_FILE = 0x31,
    PUSH_DELETE_FILE = 0x32,
    RESPONSE      = 0xFF,
};

std::string opcodeToString(Opcode op);

enum class FieldID : uint8_t
{
    FILE_PATH    = 0x01,
    FILE_VERSION = 0x02,
    NODE_TYPE    = 0x03,
    PROCESS_NAME = 0x04,
    PORT         = 0x05,
    ACK          = 0x06,
    IS_LOCKED    = 0x07,
    TIMESTAMP    = 0x08,
    FILE_ENTRY   = 0x10,
    VER_ENTRY    = 0x11,
};

struct BinaryField
{
    FieldID              id;
    std::vector<uint8_t> value;
};

struct Message
{
    Opcode   opcode     = Opcode::RESPONSE;
    uint8_t  flags      = 0;
    uint32_t seq        = 0;   // per-message sequence number (transparent)
    uint32_t payloadLen = 0;
    std::vector<BinaryField> fields;

    const BinaryField* findField(FieldID id) const noexcept;
    std::string getString(FieldID id, const std::string &def = "") const;
    uint32_t    getUint32(FieldID id, uint32_t def = 0)             const;
    uint64_t    getUint64(FieldID id, uint64_t def = 0)             const;
    bool        getBool  (FieldID id, bool def = false)             const;
    std::map<std::string, int> getFileVersionMap()                  const;

    bool isResponse() const noexcept { return (flags & FLAG_RESPONSE) != 0; }
    bool isAck()      const noexcept { return isResponse() && getBool(FieldID::ACK, false); }

    std::string toString() const;
};

class SquidProtocolFormatter
{
public:
    SquidProtocolFormatter() = default;
    explicit SquidProtocolFormatter(std::string nodeType)
        : nodeType_(std::move(nodeType)) {}

    std::vector<uint8_t> identifyFormat()      const;
    std::vector<uint8_t> closeFormat()         const;
    std::vector<uint8_t> connectServerFormat() const;
    std::vector<uint8_t> heartbeatFormat()     const;
    std::vector<uint8_t> syncStatusFormat()    const;

    std::vector<uint8_t> createFileFormat(const std::string &filePath)               const;
    std::vector<uint8_t> createFileFormat(const std::string &filePath, int version)  const;
    std::vector<uint8_t> readFileFormat(const std::string &filePath)                 const;
    std::vector<uint8_t> updateFileFormat(const std::string &filePath)               const;
    std::vector<uint8_t> updateFileFormat(const std::string &filePath, int version)  const;
    std::vector<uint8_t> deleteFileFormat(const std::string &filePath)               const;

    std::vector<uint8_t> acquireLockFormat(const std::string &filePath) const;
    std::vector<uint8_t> releaseLockFormat(const std::string &filePath) const;

    // Dedicated push-opcode frames: no ACK handshake, receiver identifies them
    // by the PUSH_* opcode so they cannot be confused with request/response.
    std::vector<uint8_t> pushCreateFileFormat(const std::string &filePath, int version) const;
    std::vector<uint8_t> pushUpdateFileFormat(const std::string &filePath, int version) const;
    std::vector<uint8_t> pushDeleteFileFormat(const std::string &filePath)               const;

    std::vector<uint8_t> responseAck(bool isAck)                                        const;
    std::vector<uint8_t> responseAckWithVersion(bool isAck, int version)                const;
    std::vector<uint8_t> responsePort(int port)                                         const;
    std::vector<uint8_t> responseIdentity(const std::string &nodeType,
                                           const std::string &processName)              const;
    std::vector<uint8_t> responseFileVersionMap(const std::map<std::string, int> &map)  const;

    std::vector<uint8_t> responseFormat(bool isAck)                                     const { return responseAck(isAck); }
    std::vector<uint8_t> responseFormat(const std::string &ack)                         const;
    std::vector<uint8_t> responseFormat(int port)                                       const { return responsePort(port); }
    std::vector<uint8_t> responseFormat(const std::string &nodeType,
                                         const std::string &processName)                const
                                        { return responseIdentity(nodeType, processName); }
    std::vector<uint8_t> responseFormat(const std::map<std::string, int> &m)            const
                                        { return responseFileVersionMap(m); }
    std::vector<uint8_t> responseFormat(const std::map<std::string, long long> &fileTimeMap)         const;
    std::vector<uint8_t> responseFormat(const std::map<std::string, fs::file_time_type> &filesLastWrite) const;

    // Stamp a seq number into an already-built frame (bytes [9..12]).
    // Called by SquidProtocol::sendFrame so that the formatter itself remains
    // stateless — seq state lives in the protocol layer.
    static void stampSeq(std::vector<uint8_t> &frame, uint32_t seq);

    Message parseMessage(const std::vector<uint8_t> &frame) const;
    Message makeNack() const;
    Message makeAck()  const;

private:
    std::string nodeType_;

    std::vector<uint8_t> buildFrame(Opcode opcode,
                                    uint8_t flags,
                                    const std::vector<BinaryField> &fields,
                                    uint32_t payloadLen = 0) const;

    static BinaryField fieldString(FieldID id, const std::string &s);
    static BinaryField fieldUint32(FieldID id, uint32_t v);
    static BinaryField fieldUint64(FieldID id, uint64_t v);
    static BinaryField fieldBool  (FieldID id, bool v);
};
