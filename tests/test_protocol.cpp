#include <gtest/gtest.h>

#include <filesystem>

#include "squidprotocol.hpp"
#include "squidProtocolFormatter.hpp"
#include "TestChannel.hpp"

namespace fs = std::filesystem;

class SquidProtocolTest : public ::testing::Test {
protected:
    SquidProtocolTest() {
        // Set up env before any FileManager construction
        char tmpl[] = "/tmp/squid_test_proto_XXXXXX";
        char *result = mkdtemp(tmpl);
        if (!result)
            std::abort();
        tmpDir_ = result;
        setenv("SQUID_STORAGE_ROOT", tmpDir_.c_str(), 1);
    }

    ~SquidProtocolTest() override {
        unsetenv("SQUID_STORAGE_ROOT");
        fs::remove_all(tmpDir_);
    }

    fs::path tmpDir_;
    FileManager fm_;
};

TEST_F(SquidProtocolTest, SendFrameStampsSeq) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test_node");
    SquidProtocolFormatter fmt;

    proto.sendFrame(fmt.heartbeatFormat());
    ASSERT_GE(ch->written().size(), 13u);

    // Seq bytes [5..8] should be 0 (first call has seq=0)
    EXPECT_EQ(ch->written()[5], 0u);
    EXPECT_EQ(ch->written()[8], 0u);

    proto.sendFrame(fmt.heartbeatFormat());
    // Now seq should be 1
    EXPECT_EQ(ch->written()[13 + 5], 0u);
    EXPECT_EQ(ch->written()[13 + 8], 1u);
}

TEST_F(SquidProtocolTest, ReceiveAndParseInOrder) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");
    SquidProtocolFormatter fmt;

    auto frame = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(frame, 0);
    ch->feed(frame);

    Message msg = proto.receiveAndParse();
    EXPECT_EQ(msg.opcode, Opcode::HEARTBEAT);
    EXPECT_EQ(msg.seq, 0u);
    EXPECT_TRUE(proto.isAlive());
}

TEST_F(SquidProtocolTest, ReceiveAndParseOutOfOrder) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");
    SquidProtocolFormatter fmt;

    // Feed frame with seq=1 first (out of order), then seq=0
    auto f1 = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(f1, 1);
    ch->feed(f1);

    auto f0 = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(f0, 0);
    ch->feed(f0);

    // First receiveAndParse: should get seq=0 (buffered seq=1, then read seq=0)
    Message m0 = proto.receiveAndParse();
    EXPECT_EQ(m0.seq, 0u);

    // Second call: should get seq=1 from the reorder buffer
    Message m1 = proto.receiveAndParse();
    EXPECT_EQ(m1.seq, 1u);
}

TEST_F(SquidProtocolTest, StaleFrameDiscarded) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");
    SquidProtocolFormatter fmt;

    // Feed seq=2 first (gap: expected seq is 0, so 2 is buffered)
    auto f2 = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(f2, 2);
    ch->feed(f2);

    // Feed seq=0 (fills the gap)
    auto f0 = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(f0, 0);
    ch->feed(f0);

    // receiveAndParse consumes seq=0
    EXPECT_EQ(proto.receiveAndParse().seq, 0u);

    // Now expected seq is 1. Feed a stale frame (seq=0 should be discarded)
    auto stale = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(stale, 0);
    ch->feed(stale);

    // Feed seq=1 (needed to unblock the buffer)
    auto f1 = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(f1, 1);
    ch->feed(f1);

    // should get seq=1, not the stale seq=0
    EXPECT_EQ(proto.receiveAndParse().seq, 1u);
    // Now should get seq=2 from buffer
    EXPECT_EQ(proto.receiveAndParse().seq, 2u);
}

TEST_F(SquidProtocolTest, SendFileDataRoundTrip) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");

    std::vector<uint8_t> data = {'h', 'e', 'l', 'l', 'o'};
    ASSERT_TRUE(proto.sendFileData(data));

    // Feed the written data back for reading
    ch->feed(ch->takeWritten());

    std::vector<uint8_t> received;
    ASSERT_TRUE(proto.receiveFileData(received));
    ASSERT_EQ(received.size(), 5u);
    EXPECT_EQ(received[0], 'h');
    EXPECT_EQ(received[4], 'o');
}

TEST_F(SquidProtocolTest, ConnectServerSendsCorrectOpcode) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");
    SquidProtocolFormatter fmt;

    proto.sendFrame(fmt.connectServerFormat());
    auto written = ch->takeWritten();
    ASSERT_GE(written.size(), 13u);
    EXPECT_EQ(written[2], static_cast<uint8_t>(Opcode::CONNECT_SERVER));
}

TEST_F(SquidProtocolTest, IsAliveDefault) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");
    EXPECT_TRUE(proto.isAlive());
}

TEST_F(SquidProtocolTest, DeadOnReadError) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");
    // No data in channel = read failure
    Message m = proto.receiveAndParse();
    EXPECT_FALSE(proto.isAlive());
    EXPECT_FALSE(m.isAck());
}

TEST_F(SquidProtocolTest, SetIsAlive) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");
    proto.setIsAlive(false);
    EXPECT_FALSE(proto.isAlive());
}

TEST_F(SquidProtocolTest, ResponseBool) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");

    proto.response(true);
    ASSERT_GE(ch->written().size(), 13u);
    EXPECT_EQ(ch->written()[2], static_cast<uint8_t>(Opcode::RESPONSE));
    EXPECT_EQ(ch->written()[3], FLAG_RESPONSE);
}

TEST_F(SquidProtocolTest, ResponseBoolWithVersion) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");

    proto.response(true, 42);
    EXPECT_EQ(ch->written()[2], static_cast<uint8_t>(Opcode::RESPONSE));
}

TEST_F(SquidProtocolTest, CloseConnMarksDead) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");

    Message m = proto.closeConn();
    EXPECT_TRUE(m.isAck());
    EXPECT_FALSE(proto.isAlive());
}

TEST_F(SquidProtocolTest, DeleteFileSendsFrame) {
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol proto(fm_, ch, "TEST", "test");

    proto.sendFrame(SquidProtocolFormatter().deleteFileFormat("/f"));
    ASSERT_GE(ch->written().size(), 13u);
    EXPECT_EQ(ch->written()[2], static_cast<uint8_t>(Opcode::DELETE_FILE));
}
