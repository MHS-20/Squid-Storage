#include <gtest/gtest.h>

#include "squidProtocolFormatter.hpp"

// ============== Message helper tests ==============

TEST(MessageTest, DefaultState) {
    Message m;
    EXPECT_EQ(m.opcode, Opcode::RESPONSE);
    EXPECT_EQ(m.flags, 0u);
    EXPECT_EQ(m.seq, 0u);
    EXPECT_EQ(m.payloadLen, 0u);
    EXPECT_TRUE(m.fields.empty());
    EXPECT_FALSE(m.isResponse());
    EXPECT_FALSE(m.isAck());
}

TEST(MessageTest, FindFieldNotFound) {
    Message m;
    EXPECT_EQ(m.findField(FieldID::FILE_PATH), nullptr);
}

TEST(MessageTest, FindFieldFound) {
    Message m;
    m.fields.push_back({FieldID::FILE_PATH, {}});
    EXPECT_NE(m.findField(FieldID::FILE_PATH), nullptr);
    EXPECT_EQ(m.findField(FieldID::FILE_VERSION), nullptr);
}

TEST(MessageTest, GetString) {
    Message m;
    std::vector<uint8_t> val = {'h', 'e', 'l', 'l', 'o'};
    m.fields.push_back({FieldID::FILE_PATH, val});
    EXPECT_EQ(m.getString(FieldID::FILE_PATH), "hello");
    EXPECT_EQ(m.getString(FieldID::FILE_VERSION, "def"), "def");
}

TEST(MessageTest, GetUint32) {
    Message m;
    std::vector<uint8_t> val = {0, 0, 0, 42};
    m.fields.push_back({FieldID::FILE_VERSION, val});
    EXPECT_EQ(m.getUint32(FieldID::FILE_VERSION), 42u);
    EXPECT_EQ(m.getUint32(FieldID::FILE_PATH, 99u), 99u);
}

TEST(MessageTest, GetUint32BigEndian) {
    Message m;
    // 0xDEADBEEF in big-endian
    std::vector<uint8_t> val = {0xDE, 0xAD, 0xBE, 0xEF};
    m.fields.push_back({FieldID::FILE_VERSION, val});
    EXPECT_EQ(m.getUint32(FieldID::FILE_VERSION), 0xDEADBEEFu);
}

TEST(MessageTest, GetUint64) {
    Message m;
    std::vector<uint8_t> val = {0, 0, 0, 0, 0, 0, 0, 99};
    m.fields.push_back({FieldID::TIMESTAMP, val});
    EXPECT_EQ(m.getUint64(FieldID::TIMESTAMP), 99u);
}

TEST(MessageTest, GetBool) {
    Message m;
    m.fields.push_back({FieldID::ACK, {0x01}});
    EXPECT_TRUE(m.getBool(FieldID::ACK));

    // default is only used when field is missing
    EXPECT_TRUE(m.getBool(FieldID::ACK, false));

    // unknown field returns default
    EXPECT_FALSE(m.getBool(FieldID::FILE_PATH, false));
    EXPECT_TRUE(m.getBool(FieldID::FILE_PATH, true));

    m.fields.clear();
    m.fields.push_back({FieldID::ACK, {0x00}});
    EXPECT_FALSE(m.getBool(FieldID::ACK));
}

TEST(MessageTest, IsResponse) {
    Message m;
    EXPECT_FALSE(m.isResponse());
    m.flags = FLAG_RESPONSE;
    EXPECT_TRUE(m.isResponse());
}

TEST(MessageTest, IsAck) {
    Message m;
    m.flags = FLAG_RESPONSE;
    m.fields.push_back({FieldID::ACK, {0x01}});
    EXPECT_TRUE(m.isAck());

    m.fields.clear();
    m.fields.push_back({FieldID::ACK, {0x00}});
    EXPECT_FALSE(m.isAck());
}

TEST(MessageTest, GetFileVersionMap) {
    Message m;
    // file1 -> v1, file2 -> v2
    std::vector<uint8_t> f1 = {'f', '1'};
    std::vector<uint8_t> f2 = {'f', '2'};
    std::vector<uint8_t> v1 = {0, 0, 0, 1};
    std::vector<uint8_t> v2 = {0, 0, 0, 2};

    m.fields.push_back({FieldID::FILE_ENTRY, f1});
    m.fields.push_back({FieldID::VER_ENTRY, v1});
    m.fields.push_back({FieldID::FILE_ENTRY, f2});
    m.fields.push_back({FieldID::VER_ENTRY, v2});

    auto map = m.getFileVersionMap();
    ASSERT_EQ(map.size(), 2u);
    EXPECT_EQ(map["f1"], 1);
    EXPECT_EQ(map["f2"], 2);
}

// ============== Formatter tests ==============

class FormatterTest : public ::testing::Test {
protected:
    SquidProtocolFormatter fmt_{"TEST_NODE"};
};

TEST_F(FormatterTest, identifyFormat) {
    auto frame = fmt_.identifyFormat();
    ASSERT_GE(frame.size(), 13u);

    const uint8_t *p = frame.data();
    // magic = 0x5351
    EXPECT_EQ(p[0], 0x53);
    EXPECT_EQ(p[1], 0x51);
    // opcode = IDENTIFY (0x02)
    EXPECT_EQ(p[2], static_cast<uint8_t>(Opcode::IDENTIFY));
    // flags = 0
    EXPECT_EQ(p[3], 0u);
    // nfields = 0
    EXPECT_EQ(p[4], 0u);
    // seq placeholder = 0
    EXPECT_EQ(p[5], 0u);
    EXPECT_EQ(p[8], 0u);
    // payloadLen = 0
    EXPECT_EQ(p[9], 0u);
    EXPECT_EQ(p[12], 0u);
}

TEST_F(FormatterTest, closeFormat) {
    auto frame = fmt_.closeFormat();
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::CLOSE));
}

TEST_F(FormatterTest, heartbeatFormat) {
    auto frame = fmt_.heartbeatFormat();
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::HEARTBEAT));
}

TEST_F(FormatterTest, connectServerFormat) {
    auto frame = fmt_.connectServerFormat();
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::CONNECT_SERVER));
}

TEST_F(FormatterTest, syncStatusFormat) {
    auto frame = fmt_.syncStatusFormat();
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::SYNC_STATUS));
}

TEST_F(FormatterTest, createFileFormat) {
    auto frame = fmt_.createFileFormat("/test.txt");
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::CREATE_FILE));
    // 1 field (FILE_PATH)
    EXPECT_EQ(frame[4], 1u);
    // payload should be non-zero
    EXPECT_GT(frame.size(), 13u);
}

TEST_F(FormatterTest, createFileFormatWithVersion) {
    auto frame = fmt_.createFileFormat("/test.txt", 7);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::CREATE_FILE));
    // 2 fields (FILE_PATH + FILE_VERSION)
    EXPECT_EQ(frame[4], 2u);
}

TEST_F(FormatterTest, readFileFormat) {
    auto frame = fmt_.readFileFormat("/f");
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::READ_FILE));
}

TEST_F(FormatterTest, updateFileFormat) {
    auto frame = fmt_.updateFileFormat("/f");
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::UPDATE_FILE));
}

TEST_F(FormatterTest, deleteFileFormat) {
    auto frame = fmt_.deleteFileFormat("/f");
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::DELETE_FILE));
}

TEST_F(FormatterTest, lockFormats) {
    auto acquire = fmt_.acquireLockFormat("/f");
    EXPECT_EQ(acquire[2], static_cast<uint8_t>(Opcode::ACQUIRE_LOCK));

    auto release = fmt_.releaseLockFormat("/f");
    EXPECT_EQ(release[2], static_cast<uint8_t>(Opcode::RELEASE_LOCK));
}

TEST_F(FormatterTest, pushCreateFileFormat) {
    auto frame = fmt_.pushCreateFileFormat("/f", 1);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::PUSH_CREATE_FILE));
}

TEST_F(FormatterTest, pushUpdateFileFormat) {
    auto frame = fmt_.pushUpdateFileFormat("/f", 2);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::PUSH_UPDATE_FILE));
}

TEST_F(FormatterTest, pushDeleteFileFormat) {
    auto frame = fmt_.pushDeleteFileFormat("/f");
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::PUSH_DELETE_FILE));
}

TEST_F(FormatterTest, leaderHbFormat) {
    auto frame = fmt_.leaderHbFormat(42);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::LEADER_HB));
    // Should contain EPOCH field
    EXPECT_EQ(frame[4], 1u);
}

TEST_F(FormatterTest, nackStaleEpochFormat) {
    auto frame = fmt_.nackStaleEpochFormat(99);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::NACK_STALE_EPOCH));
}

// ============== Response formatting ==============

TEST_F(FormatterTest, responseAck) {
    auto frame = fmt_.responseAck(true);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::RESPONSE));
    EXPECT_EQ(frame[3], FLAG_RESPONSE);
    EXPECT_EQ(frame[4], 1u); // one field (ACK bool)
}

TEST_F(FormatterTest, responseNack) {
    auto frame = fmt_.responseAck(false);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::RESPONSE));
    EXPECT_EQ(frame[3], FLAG_RESPONSE);
}

TEST_F(FormatterTest, responseAckWithVersion) {
    auto frame = fmt_.responseAckWithVersion(true, 42);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::RESPONSE));
    EXPECT_EQ(frame[4], 2u); // ACK + FILE_VERSION
}

TEST_F(FormatterTest, responsePort) {
    auto frame = fmt_.responsePort(8080);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::RESPONSE));
}

TEST_F(FormatterTest, responseIdentity) {
    auto frame = fmt_.responseIdentity("CLIENT", "test_proc");
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::RESPONSE));
    EXPECT_EQ(frame[4], 2u); // NODE_TYPE + PROCESS_NAME
}

TEST_F(FormatterTest, responseFileVersionMap) {
    std::map<std::string, int> m = {{"a", 1}, {"b", 2}};
    auto frame = fmt_.responseFileVersionMap(m);
    EXPECT_EQ(frame[4], 4u); // 2 FILE_ENTRY + 2 VER_ENTRY
}

// ============== stampSeq ==============

TEST_F(FormatterTest, stampSeq) {
    auto frame = fmt_.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(frame, 0xDEADBEEF);
    EXPECT_EQ(frame[5], 0xDE);
    EXPECT_EQ(frame[6], 0xAD);
    EXPECT_EQ(frame[7], 0xBE);
    EXPECT_EQ(frame[8], 0xEF);
}

TEST_F(FormatterTest, stampSeqOnEmptyFrame) {
    std::vector<uint8_t> empty;
    SquidProtocolFormatter::stampSeq(empty, 42); // should not crash
    EXPECT_TRUE(empty.empty());
}

// ============== Parse round-trip ==============

TEST_F(FormatterTest, ParseRoundTripSimple) {
    // Build a create-file frame, parse it back.
    auto frame = fmt_.createFileFormat("/path/to/file.txt");
    SquidProtocolFormatter::stampSeq(frame, 7);

    auto msg = fmt_.parseMessage(frame);
    EXPECT_EQ(msg.opcode, Opcode::CREATE_FILE);
    EXPECT_EQ(msg.flags, 0u);
    EXPECT_EQ(msg.seq, 7u);
    ASSERT_EQ(msg.fields.size(), 1u);
    EXPECT_EQ(msg.fields[0].id, FieldID::FILE_PATH);
    EXPECT_EQ(msg.getString(FieldID::FILE_PATH), "/path/to/file.txt");
}

TEST_F(FormatterTest, ParseRoundTripWithVersion) {
    auto frame = fmt_.createFileFormat("/f", 99);
    SquidProtocolFormatter::stampSeq(frame, 3);

    auto msg = fmt_.parseMessage(frame);
    EXPECT_EQ(msg.opcode, Opcode::CREATE_FILE);
    EXPECT_EQ(msg.seq, 3u);
    ASSERT_EQ(msg.fields.size(), 2u);
    EXPECT_EQ(msg.getString(FieldID::FILE_PATH), "/f");
    EXPECT_EQ(msg.getUint32(FieldID::FILE_VERSION), 99u);
}

TEST_F(FormatterTest, ParseRoundTripResponseAck) {
    auto frame = fmt_.responseAck(true);
    SquidProtocolFormatter::stampSeq(frame, 1);

    auto msg = fmt_.parseMessage(frame);
    EXPECT_EQ(msg.opcode, Opcode::RESPONSE);
    EXPECT_TRUE(msg.isResponse());
    EXPECT_TRUE(msg.isAck());
    EXPECT_EQ(msg.seq, 1u);
}

TEST_F(FormatterTest, ParseRoundTripResponseNack) {
    auto frame = fmt_.responseAck(false);
    auto msg = fmt_.parseMessage(frame);
    EXPECT_TRUE(msg.isResponse());
    EXPECT_FALSE(msg.isAck());
}

TEST_F(FormatterTest, parseMessageBadMagic) {
    std::vector<uint8_t> bad(13, 0);
    EXPECT_THROW(fmt_.parseMessage(bad), std::runtime_error);
}

TEST_F(FormatterTest, parseMessageTooShort) {
    std::vector<uint8_t> bad(5, 0);
    EXPECT_THROW(fmt_.parseMessage(bad), std::runtime_error);
}

TEST_F(FormatterTest, stateSnapFormat) {
    std::map<std::string, int> vm = {{"f1", 1}, {"f2", 2}};
    std::map<std::string, std::set<std::string>> rm = {
        {"f1", {"dn1"}},
        {"f2", {"dn1", "dn2"}},
    };
    auto frame = fmt_.stateSnapFormat(vm, rm, 5);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::STATE_SNAP));

    // Parse it back
    auto msg = fmt_.parseMessage(frame);
    EXPECT_EQ(msg.opcode, Opcode::STATE_SNAP);
    EXPECT_EQ(msg.getUint32(FieldID::EPOCH), 5u);
    // 1 EPOCH + 2 SNAP_ENTRY = 3 fields
    EXPECT_EQ(msg.fields.size(), 3u);
}

TEST_F(FormatterTest, stateDeltaFormat) {
    std::vector<std::string> dns = {"dn1", "dn2"};
    auto frame = fmt_.stateDeltaFormat(0, "/f", 3, dns, 7);
    EXPECT_EQ(frame[2], static_cast<uint8_t>(Opcode::STATE_DELTA));

    auto msg = fmt_.parseMessage(frame);
    EXPECT_EQ(msg.opcode, Opcode::STATE_DELTA);
    EXPECT_EQ(msg.getUint32(FieldID::EPOCH), 7u);
    EXPECT_EQ(msg.getString(FieldID::FILE_PATH), "/f");
    EXPECT_EQ(msg.getUint32(FieldID::FILE_VERSION), 3u);
}

// ============== parseSnapEntry ==============

TEST(ParseSnapEntry, Basic) {
    std::string path;
    int version;
    std::set<std::string> dns;

    EXPECT_TRUE(SquidProtocolFormatter::parseSnapEntry("f.txt 2 dn1,dn2", path, version, dns));
    EXPECT_EQ(path, "f.txt");
    EXPECT_EQ(version, 2);
    ASSERT_EQ(dns.size(), 2u);
    EXPECT_TRUE(dns.count("dn1"));
    EXPECT_TRUE(dns.count("dn2"));
}

TEST(ParseSnapEntry, NoDatanodes) {
    std::string path;
    int version;
    std::set<std::string> dns;

    EXPECT_TRUE(SquidProtocolFormatter::parseSnapEntry("f.txt 0", path, version, dns));
    EXPECT_EQ(path, "f.txt");
    EXPECT_EQ(version, 0);
    EXPECT_TRUE(dns.empty());
}

TEST(ParseSnapEntry, SpacesInPath) {
    std::string path;
    int version;
    std::set<std::string> dns;

    // istringstream >> splits on whitespace, so paths with spaces break.
    // This is a known limitation of the format.
    EXPECT_FALSE(SquidProtocolFormatter::parseSnapEntry("my path 1 dn1", path, version, dns));
}

TEST(ParseSnapEntry, Malformed) {
    std::string path;
    int version;
    std::set<std::string> dns;

    EXPECT_FALSE(SquidProtocolFormatter::parseSnapEntry("", path, version, dns));
    EXPECT_FALSE(SquidProtocolFormatter::parseSnapEntry("no_version", path, version, dns));
}

// ============== Opcode toString ==============

TEST(Opcodes, OpcodeToString) {
    EXPECT_EQ(opcodeToString(Opcode::CREATE_FILE), "CREATE_FILE");
    EXPECT_EQ(opcodeToString(Opcode::READ_FILE), "READ_FILE");
    EXPECT_EQ(opcodeToString(Opcode::UPDATE_FILE), "UPDATE_FILE");
    EXPECT_EQ(opcodeToString(Opcode::DELETE_FILE), "DELETE_FILE");
    EXPECT_EQ(opcodeToString(Opcode::ACQUIRE_LOCK), "ACQUIRE_LOCK");
    EXPECT_EQ(opcodeToString(Opcode::RELEASE_LOCK), "RELEASE_LOCK");
    EXPECT_EQ(opcodeToString(Opcode::HEARTBEAT), "HEARTBEAT");
    EXPECT_EQ(opcodeToString(Opcode::RESPONSE), "RESPONSE");
    EXPECT_EQ(opcodeToString(Opcode::IDENTIFY), "IDENTIFY");
    EXPECT_EQ(opcodeToString(Opcode::CLOSE), "CLOSE");
    EXPECT_EQ(opcodeToString(Opcode::CONNECT_SERVER), "CONNECT_SERVER");
    EXPECT_EQ(opcodeToString(Opcode::SYNC_STATUS), "SYNC_STATUS");
    EXPECT_EQ(opcodeToString(Opcode::PUSH_CREATE_FILE), "PUSH_CREATE_FILE");
    EXPECT_EQ(opcodeToString(Opcode::PUSH_UPDATE_FILE), "PUSH_UPDATE_FILE");
    EXPECT_EQ(opcodeToString(Opcode::PUSH_DELETE_FILE), "PUSH_DELETE_FILE");
    EXPECT_EQ(opcodeToString(Opcode::STATE_SNAP), "STATE_SNAP");
    EXPECT_EQ(opcodeToString(Opcode::STATE_DELTA), "STATE_DELTA");
    EXPECT_EQ(opcodeToString(Opcode::LEADER_HB), "LEADER_HB");
    EXPECT_EQ(opcodeToString(Opcode::NACK_STALE_EPOCH), "NACK_STALE_EPOCH");
    EXPECT_EQ(opcodeToString(static_cast<Opcode>(0xFE)), "UNKNOWN");
}
