#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ConnectionSession.hpp"
#include "squidprotocol.hpp"
#include "squidProtocolFormatter.hpp"
#include "filemanager.hpp"

// ── Real-socket bidirectional channel for select()-based tests ────────
// Wraps one end of a Unix socketpair so the session worker's select()
// loop receives a real fd and can actually process incoming data.
class SocketPairChannel : public INetworkChannel {
public:
    explicit SocketPairChannel(int fd) : fd_(fd) {}
    ~SocketPairChannel() override { if (fd_ >= 0) ::close(fd_); }

    ssize_t readBytes(uint8_t *buf, size_t len) override {
        ssize_t r = ::read(fd_, buf, len);
        return r;
    }

    ssize_t writeBytes(const uint8_t *buf, size_t len) override {
        ssize_t w = ::write(fd_, buf, len);
        return w;
    }

    int   getSocket() const override { return fd_; }
    bool  isOpen()    const override { return fd_ >= 0; }
    void  close()           override {
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

static std::pair<std::shared_ptr<SocketPairChannel>,
                 std::shared_ptr<SocketPairChannel>> makeSocketPair() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        throw std::runtime_error("socketpair failed");
    }
    return {std::make_shared<SocketPairChannel>(fds[0]),
            std::make_shared<SocketPairChannel>(fds[1])};
}

// ── Helper: build a heartbeat frame with a given seq ──────────────────
static std::vector<uint8_t> hbFrame(uint32_t seq) {
    SquidProtocolFormatter fmt;
    auto f = fmt.heartbeatFormat();
    f[5] = static_cast<uint8_t>((seq >> 24) & 0xFF);
    f[6] = static_cast<uint8_t>((seq >> 16) & 0xFF);
    f[7] = static_cast<uint8_t>((seq >>  8) & 0xFF);
    f[8] = static_cast<uint8_t>( seq        & 0xFF);
    return f;
}

// ========================================================================
// P0.1 — readSuspended_ — now checked in run() loop (FIXED)
// ========================================================================
// After suspendReads(), the worker's select() still monitors the fd but
// skips frame parsing.  This test verifies the handler is NOT called while
// reads are suspended.
TEST(StressTest, ReadSuspendedIsDeadCode) {
    auto [srv, cli] = makeSocketPair();
    std::atomic<int> count{0};

    auto handler = [&](ConnectionSession &, const Message &) {
        count.fetch_add(1);
    };

    FileManager fm;
    auto session = std::make_shared<ConnectionSession>(
        fm, cli, "CLIENT", "test_client", handler);
    session->start(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Send a frame and confirm it gets through.
    srv->writeBytes(hbFrame(0).data(), hbFrame(0).size());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    int beforeSuspend = count.load();
    ASSERT_GT(beforeSuspend, 0)
        << "Pre-condition: worker must process a frame before suspend";

    // Suspend reads and send another frame.  With the fix, the handler
    // is NOT called while reads are suspended.
    session->suspendReads();
    srv->writeBytes(hbFrame(1).data(), hbFrame(1).size());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    int afterSuspend = count.load();

    EXPECT_EQ(afterSuspend, beforeSuspend)
        << "FIXED: readSuspended_ now works — handler was NOT called after "
           "suspendReads()";

    session->stop();
}

// ========================================================================
// P0.6 — Data race on protocol_.alive_ (plain bool, cross-thread read/write)
// ========================================================================
// Worker writes alive_ during message processing; another thread reads it
// via isAlive().  Running under ThreadSanitizer should flag the race.
TEST(StressTest, AliveDataRace) {
    auto [srv, cli] = makeSocketPair();
    FileManager fm;
    auto session = std::make_shared<ConnectionSession>(
        fm, cli, "CLIENT", "test_client");
    session->start(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Feeder thread: sends heartbeat frames so the worker processes them.
    std::atomic<bool> stop{false};
    std::thread feeder([&]() {
        uint32_t seq = 0;
        while (!stop.load()) {
            auto hb = hbFrame(seq++);
            srv->writeBytes(hb.data(), hb.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // Hammer thread: tight-loop calls to isAlive().
    std::thread hammer([&]() {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            session->isAlive();
        }
    });

    hammer.join();
    stop.store(true);
    feeder.join();
    session->stop();
}

// ========================================================================
// P0.10 — FileManager not thread-safe (data race on std::map)
// ========================================================================
// Multiple threads call setFileVersion / getFileVersionMap / etc.
// concurrently with no mutex.  TSan should flag the unsynchronised access.
TEST(StressTest, FileManagerConcurrentAccess) {
    FileManager fm;
    constexpr int N_THREADS = 4;
    std::vector<std::thread> threads;

    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&fm, t]() {
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < deadline) {
                std::string path = "f_" + std::to_string(t) + "_" +
                                   std::to_string(::rand() % 100);
                switch (::rand() % 4) {
                case 0: fm.setFileVersion(path, ::rand());           break;
                case 1: fm.getFileVersionMap(path);                  break;
                case 2: fm.deleteFileAndVersion(path);                break;
                case 3: fm.getFileVersion(path);                     break;
                }
            }
        });
    }

    for (auto &th : threads)
        th.join();

    SUCCEED();
}

// ========================================================================
// P0.7 — No bound on wire payloadLen (OOM risk)
// ========================================================================
// Wire a frame with payloadLen=128 MiB into the receiver.  The current
// code attempts to allocate without any limit check — proving the gap.
TEST(StressTest, PayloadLenUnchecked) {
    auto [srv, cli] = makeSocketPair();
    FileManager fm;
    SquidProtocol proto(fm, cli, "SERVER", "test_server");

    std::vector<uint8_t> frame;
    frame.push_back(0x53); frame.push_back(0x51);  // magic
    frame.push_back(static_cast<uint8_t>(Opcode::HEARTBEAT));
    frame.push_back(0x00);                          // flags
    frame.push_back(0x00);                          // nfields
    frame.resize(frame.size() + 4, 0);              // seq
    // payloadLen = 16 MiB (0x01000000)
    frame.push_back(0x08); frame.push_back(0x00);
    frame.push_back(0x00); frame.push_back(0x00);

    srv->writeBytes(frame.data(), frame.size());
    srv->close();  // EOF so recvExact doesn't block

    Message msg = proto.receiveAndParse();
    EXPECT_FALSE(proto.isAlive())
        << "Protocol should die parsing an oversized frame";
    (void)msg;
}

// ========================================================================
// P2.2 — sendFrame consumes seq number on write failure
// ========================================================================
// sendFrame incremements nextSendSeq_ before attempting the write.
// If the write fails, the seq is consumed but the frame was never sent.
TEST(StressTest, SendFrameSeqConsumedOnFailure) {
    // Use a real socket pair, then close the read end so writes fail.
    auto [srv, cli] = makeSocketPair();
    srv->close();  // kill the remote end

    FileManager fm;
    SquidProtocol proto(fm, cli, "CLIENT", "test_client");

    proto.heartbeat();
    EXPECT_FALSE(proto.isAlive())
        << "Protocol should be dead after send failure";
}
