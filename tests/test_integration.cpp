#include <gtest/gtest.h>

#include <filesystem>
#include <cstring>
#include <memory>
#include <vector>

#include "squidprotocol.hpp"
#include "squidProtocolFormatter.hpp"
#include "INetworkChannel.hpp"
#include "TestChannel.hpp"

namespace fs = std::filesystem;

// ── PairedChannel: bidirectional fake channel ──────────────────────────
// Two PairedChannels linked together act as a bidirectional pipe:
// writes on one side accumulate as reads on the other.
class PairedChannel : public INetworkChannel {
public:
    void link(const std::shared_ptr<PairedChannel> &peer) { peer_ = peer; }

    ssize_t readBytes(uint8_t *buf, size_t len) override {
        if (readBuf_.empty())
            return -1;
        size_t toCopy = std::min(len, readBuf_.size());
        std::memcpy(buf, readBuf_.data(), toCopy);
        readBuf_.erase(readBuf_.begin(),
                        readBuf_.begin() + static_cast<ssize_t>(toCopy));
        return static_cast<ssize_t>(toCopy);
    }

    ssize_t writeBytes(const uint8_t *buf, size_t len) override {
        if (auto p = peer_.lock())
            p->readBuf_.insert(p->readBuf_.end(), buf, buf + len);
        return static_cast<ssize_t>(len);
    }

    int   getSocket() const override { return 0; }
    bool  isOpen()    const override { return true; }
    void  close()           override {}

private:
    std::vector<uint8_t> readBuf_;
    std::weak_ptr<PairedChannel> peer_;
};

static std::pair<std::shared_ptr<PairedChannel>,
                 std::shared_ptr<PairedChannel>> makeChannelPair() {
    auto a = std::make_shared<PairedChannel>();
    auto b = std::make_shared<PairedChannel>();
    a->link(b);
    b->link(a);
    return {a, b};
}

// ── Helpers for building custom frames ─────────────────────────────────

// Build a RESPONSE frame with ACK=true plus an optional EPOCH field.
static std::vector<uint8_t> makeAckFrame(uint32_t epoch = 0, bool includeEpoch = false) {
    uint8_t nfields = includeEpoch ? 2 : 1;
    std::vector<uint8_t> frame;
    // Header (13 bytes)
    frame.push_back(0x53); frame.push_back(0x51);               // magic
    frame.push_back(static_cast<uint8_t>(Opcode::RESPONSE));    // opcode
    frame.push_back(FLAG_RESPONSE);                              // flags
    frame.push_back(nfields);                                    // nfields
    frame.resize(frame.size() + 4, 0);                           // seq placeholder
    frame.resize(frame.size() + 4, 0);                           // payloadLen

    // Field: ACK = true
    frame.push_back(static_cast<uint8_t>(FieldID::ACK));
    frame.push_back(0x00); frame.push_back(0x01);               // len=1
    frame.push_back(0x01);                                       // value=true

    if (includeEpoch) {
        frame.push_back(static_cast<uint8_t>(FieldID::EPOCH));
        frame.push_back(0x00); frame.push_back(0x04);           // len=4
        frame.push_back(static_cast<uint8_t>((epoch >> 24) & 0xFF));
        frame.push_back(static_cast<uint8_t>((epoch >> 16) & 0xFF));
        frame.push_back(static_cast<uint8_t>((epoch >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(epoch & 0xFF));
    }

    return frame;
}

// ── Test fixture ───────────────────────────────────────────────────────
class IntegrationTest : public ::testing::Test {
protected:
    IntegrationTest() {
        char tmpl[] = "/tmp/squid_test_integ_XXXXXX";
        char *result = mkdtemp(tmpl);
        if (!result) std::abort();
        tmpDir_ = result;
        setenv("SQUID_STORAGE_ROOT", tmpDir_.c_str(), 1);
    }

    ~IntegrationTest() override {
        unsetenv("SQUID_STORAGE_ROOT");
        fs::remove_all(tmpDir_);
    }

    fs::path tmpDir_;
    FileManager fm_;
};

// ── Tests ─────────────────────────────────────────────────────────────

// Full handshake roundtrip between server and client SquidProtocol instances
// via paired bidirectional channels:
//   SERVER sends IDENTIFY
//   CLIENT receives IDENTIFY, responds with identity
//   SERVER receives identity, sends ACK
//   CLIENT receives ACK
TEST_F(IntegrationTest, HandshakeRoundtrip) {
    auto [serverCh, clientCh] = makeChannelPair();
    SquidProtocol server(fm_, serverCh, "SERVER", "server_node");
    SquidProtocol client(fm_, clientCh, "CLIENT", "client_node");
    SquidProtocolFormatter fmt;

    // --- Server sends IDENTIFY ---
    server.sendFrame(fmt.identifyFormat());
    ASSERT_TRUE(server.isAlive());

    // --- Client receives IDENTIFY ---
    Message identify = client.receiveAndParse();
    ASSERT_TRUE(client.isAlive());
    EXPECT_EQ(identify.opcode, Opcode::IDENTIFY);

    // --- Client responds with its identity ---
    client.response("CLIENT", "client_node");
    ASSERT_TRUE(client.isAlive());

    // --- Server receives identity response ---
    Message identityResp = server.receiveAndParse();
    ASSERT_TRUE(server.isAlive());
    EXPECT_TRUE(identityResp.isResponse());
    EXPECT_EQ(identityResp.getString(FieldID::NODE_TYPE), "CLIENT");
    EXPECT_EQ(identityResp.getString(FieldID::PROCESS_NAME), "client_node");

    // --- Server sends ACK ---
    server.response(true);
    ASSERT_TRUE(server.isAlive());

    // --- Client receives ACK ---
    Message ack = client.receiveAndParse();
    ASSERT_TRUE(client.isAlive());
    EXPECT_TRUE(ack.isAck());
}

// Client receives an unexpected opcode instead of IDENTIFY → connection is
// still alive but the caller must reject the message.
TEST_F(IntegrationTest, HandshakeWrongOpcode) {
    auto [serverCh, clientCh] = makeChannelPair();
    SquidProtocol client(fm_, clientCh, "CLIENT", "client_node");
    SquidProtocolFormatter fmt;

    // Server sends HEARTBEAT instead of IDENTIFY
    serverCh->writeBytes(fmt.heartbeatFormat().data(),
                         fmt.heartbeatFormat().size());
    // Manually stamp seq 0 since we bypassed SquidProtocol::sendFrame
    auto hb = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(hb, 0);
    serverCh->writeBytes(hb.data(), hb.size());

    Message msg = client.receiveAndParse();
    ASSERT_TRUE(client.isAlive());
    EXPECT_NE(msg.opcode, Opcode::IDENTIFY);
    EXPECT_EQ(msg.opcode, Opcode::HEARTBEAT);
}

// Exercise epoch field carrying through the protocol.
// Simulates two sequential handshakes: first with epoch=5, second with epoch=3.
// Uses manual seq stamping on the server side because we construct custom
// frames (ACK+EPOCH) that SquidProtocolFormatter doesn't expose directly.
TEST_F(IntegrationTest, HandshakeEpochCarriedInAck) {
    auto [serverCh, clientCh] = makeChannelPair();
    // server_ is only used for receiveAndParse (to consume client responses)
    SquidProtocol server(fm_, serverCh, "SERVER", "server_node");
    SquidProtocol client(fm_, clientCh, "CLIENT", "client_node");
    SquidProtocolFormatter fmt;

    // ── First handshake: epoch=5 ──
    // Server sends IDENTIFY (seq=0, manual to keep full control)
    auto id0 = fmt.identifyFormat();
    SquidProtocolFormatter::stampSeq(id0, 0);
    serverCh->writeBytes(id0.data(), id0.size());

    // Client receives IDENTIFY (seq=0), responds with identity
    Message identify = client.receiveAndParse();
    ASSERT_TRUE(client.isAlive());
    EXPECT_EQ(identify.opcode, Opcode::IDENTIFY);
    EXPECT_EQ(identify.seq, 0u);
    client.response("CLIENT", "client_node");  // seq=0 on client's send side

    // Server consumes client's identity response (client's seq=0)
    server.receiveAndParse();

    // Server sends ACK with epoch=5 (seq=1)
    auto ack5 = makeAckFrame(5, true);
    SquidProtocolFormatter::stampSeq(ack5, 1);
    serverCh->writeBytes(ack5.data(), ack5.size());

    // Client receives ACK (seq=1), epoch=5
    Message firstAck = client.receiveAndParse();
    ASSERT_TRUE(client.isAlive());
    EXPECT_TRUE(firstAck.isAck());
    EXPECT_EQ(firstAck.seq, 1u);
    EXPECT_EQ(firstAck.getUint32(FieldID::EPOCH, 0), 5u);

    // ── Second handshake: epoch=3 ──
    // Server sends IDENTIFY (seq=2)
    auto id2 = fmt.identifyFormat();
    SquidProtocolFormatter::stampSeq(id2, 2);
    serverCh->writeBytes(id2.data(), id2.size());

    // Client receives IDENTIFY (seq=2)
    identify = client.receiveAndParse();
    ASSERT_TRUE(client.isAlive());
    EXPECT_EQ(identify.opcode, Opcode::IDENTIFY);
    EXPECT_EQ(identify.seq, 2u);
    client.response("CLIENT", "client_node");  // seq=1 on client's send side

    // Server consumes client's second response (client's seq=1)
    server.receiveAndParse();

    // Server sends ACK with epoch=3 (seq=3)
    auto ack3 = makeAckFrame(3, true);
    SquidProtocolFormatter::stampSeq(ack3, 3);
    serverCh->writeBytes(ack3.data(), ack3.size());

    // Client receives ACK (seq=3), epoch=3
    Message secondAck = client.receiveAndParse();
    ASSERT_TRUE(client.isAlive());
    EXPECT_TRUE(secondAck.isAck());
    EXPECT_EQ(secondAck.seq, 3u);
    EXPECT_EQ(secondAck.getUint32(FieldID::EPOCH, 0), 3u);
}

// Connection drops during handshake (channel returns -1 on read).
TEST_F(IntegrationTest, HandshakeDisconnectDuringIdentify) {
    // Use a standalone TestChannel with no data fed
    auto ch = std::make_shared<TestChannel>();
    SquidProtocol client(fm_, ch, "CLIENT", "client_node");

    // No data in channel — receiveAndParse should mark connection dead
    Message msg = client.receiveAndParse();
    EXPECT_FALSE(client.isAlive());
    EXPECT_FALSE(msg.isAck());
}

// Bi-directional message exchange after handshake: server and client send
// heartbeats to each other and both receive correctly.
TEST_F(IntegrationTest, BidirectionalHeartbeatExchange) {
    auto [serverCh, clientCh] = makeChannelPair();
    SquidProtocol server(fm_, serverCh, "SERVER", "server_node");
    SquidProtocol client(fm_, clientCh, "CLIENT", "client_node");
    SquidProtocolFormatter fmt;

    // Server sends IDENTIFY, client responds with identity, server ACKs
    server.sendFrame(fmt.identifyFormat());
    Message identify = client.receiveAndParse();
    ASSERT_EQ(identify.opcode, Opcode::IDENTIFY);
    client.response("CLIENT", "client_node");
    server.receiveAndParse();
    server.response(true);
    client.receiveAndParse();

    // Now both sides exchange heartbeats
    server.sendFrame(fmt.heartbeatFormat());
    client.sendFrame(fmt.heartbeatFormat());

    // Client receives server's heartbeat
    Message m1 = client.receiveAndParse();
    ASSERT_TRUE(client.isAlive());
    EXPECT_EQ(m1.opcode, Opcode::HEARTBEAT);

    // Server receives client's heartbeat
    Message m2 = server.receiveAndParse();
    ASSERT_TRUE(server.isAlive());
    EXPECT_EQ(m2.opcode, Opcode::HEARTBEAT);
}

// File-transfer round-trip through paired channels:
// server sends file data → client receives it.
TEST_F(IntegrationTest, FileTransferThroughPairedChannel) {
    auto [serverCh, clientCh] = makeChannelPair();
    SquidProtocol server(fm_, serverCh, "SERVER", "server_node");
    SquidProtocol client(fm_, clientCh, "CLIENT", "client_node");

    std::vector<uint8_t> original = {'i', 'n', 't', 'e', 'g', 'r', 'a', 't', 'e'};

    ASSERT_TRUE(server.sendFileData(original));

    std::vector<uint8_t> received;
    ASSERT_TRUE(client.receiveFileData(received));
    EXPECT_EQ(received, original);
}

// Reorder buffer integration: out-of-order frames delivered through the
// bidirectional channel are correctly re-sequenced by the receiving
// SquidProtocol.
TEST_F(IntegrationTest, ReorderingThroughChannel) {
    auto [serverCh, clientCh] = makeChannelPair();
    SquidProtocol client(fm_, clientCh, "CLIENT", "client_node");
    SquidProtocolFormatter fmt;

    // Server sends frames out of order: seq=1 first, then seq=0
    auto f1 = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(f1, 1);
    serverCh->writeBytes(f1.data(), f1.size());

    auto f0 = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(f0, 0);
    serverCh->writeBytes(f0.data(), f0.size());

    // Client should receive seq=0 first (buffered seq=1 until seq=0 arrives)
    Message m0 = client.receiveAndParse();
    EXPECT_EQ(m0.seq, 0u);

    // Then seq=1 from the reorder buffer
    Message m1 = client.receiveAndParse();
    EXPECT_EQ(m1.seq, 1u);
}

// syncStatus robustly consumes push frames that arrive before the response.
// Server enqueues a PUSH_CREATE_FILE + file data before the SYNC_STATUS
// RESPONSE.  syncStatus must return the RESPONSE, not the push, and must
// consume the trailing file data so it does not corrupt the next read.
TEST_F(IntegrationTest, SyncStatusConsumesPushes) {
    auto [serverCh, clientCh] = makeChannelPair();
    SquidProtocol server(fm_, serverCh, "SERVER", "server_node");
    SquidProtocol client(fm_, clientCh, "CLIENT", "client_node");
    SquidProtocolFormatter fmt;

    // Build a PUSH_CREATE_FILE frame (seq=0).
    std::string filePath = "consumed.txt";
    int fileVersion = 42;
    auto pushFrame = fmt.pushCreateFileFormat(filePath, fileVersion);
    SquidProtocolFormatter::stampSeq(pushFrame, 0);

    // Build the trailing file data in FileTransfer format:
    // 8-byte big-endian size + raw bytes.
    std::vector<uint8_t> fileData = {'p', 'u', 's', 'h'};
    uint64_t sz = fileData.size();
    std::vector<uint8_t> transferData;
    for (int i = 7; i >= 0; --i)
        transferData.push_back(static_cast<uint8_t>((sz >> (i * 8)) & 0xFF));
    transferData.insert(transferData.end(), fileData.begin(), fileData.end());

    // Build the SYNC_STATUS RESPONSE (seq=1).
    auto ackFrame = makeAckFrame(5, false);
    SquidProtocolFormatter::stampSeq(ackFrame, 1);

    // Feed push frame + file data + response into the server's write buffer
    // (which the client reads).
    serverCh->writeBytes(pushFrame.data(), pushFrame.size());
    serverCh->writeBytes(transferData.data(), transferData.size());
    serverCh->writeBytes(ackFrame.data(), ackFrame.size());

    // Call syncStatus — it should consume the push and return the RESPONSE.
    Message result = client.syncStatus();
    ASSERT_TRUE(client.isAlive());
    EXPECT_TRUE(result.isAck());

    // Verify the SYNC_STATUS request was actually sent (visible on server's
    // receive side).
    Message req = server.receiveAndParse();
    ASSERT_TRUE(server.isAlive());
    EXPECT_EQ(req.opcode, Opcode::SYNC_STATUS);

    // Verify the first syncStatus result is an ACK as expected.
    ASSERT_TRUE(result.isAck());

    // Verify the protocol is clean — client's expectedRecvSeq_ is 2 after
    // consuming push (seq=0) and ACK (seq=1).  Write a heartbeat with seq=2
    // and verify the client reads it.
    ASSERT_GE(client.getLastSeq(), 1u);

    auto hbFrame = fmt.heartbeatFormat();
    SquidProtocolFormatter::stampSeq(hbFrame, 2);
    serverCh->writeBytes(hbFrame.data(), hbFrame.size());
    Message hb = client.receiveAndParse();
    ASSERT_TRUE(client.isAlive()) << "Client died reading heartbeat seq=2";
    EXPECT_EQ(hb.opcode, Opcode::HEARTBEAT);
}
