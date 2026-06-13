#pragma once

#include "INetworkChannel.hpp"
#include <cstring>
#include <queue>
#include <vector>

// Fake INetworkChannel for unit tests.
// Writes accumulate in written().
// Reads drain from a queue fed by feed().
class TestChannel : public INetworkChannel {
public:
    ssize_t readBytes(uint8_t *buf, size_t len) override {
        if (readQueue_.empty())
            return -1;
        auto &front = readQueue_.front();
        size_t toCopy = std::min(len, front.size());
        std::memcpy(buf, front.data(), toCopy);
        front.erase(front.begin(), front.begin() + static_cast<ssize_t>(toCopy));
        if (front.empty())
            readQueue_.pop();
        return static_cast<ssize_t>(toCopy);
    }

    ssize_t writeBytes(const uint8_t *buf, size_t len) override {
        written_.insert(written_.end(), buf, buf + len);
        return static_cast<ssize_t>(len);
    }

    int  getSocket() const override { return 0; }
    bool isOpen()    const override { return true; }
    void close()           override {}

    void feed(const std::vector<uint8_t> &data) { readQueue_.push(data); }
    const std::vector<uint8_t> &written() const { return written_; }
    std::vector<uint8_t> takeWritten() { return std::move(written_); }

private:
    std::vector<uint8_t> written_;
    std::queue<std::vector<uint8_t>> readQueue_;
};
