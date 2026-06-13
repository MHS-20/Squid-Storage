#pragma once

#include "INetworkChannel.hpp"
#include <cstring>
#include <memory>
#include <vector>

// PairedChannel: bidirectional fake channel for tests.
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

static inline std::pair<std::shared_ptr<PairedChannel>,
                         std::shared_ptr<PairedChannel>> makeChannelPair() {
    auto a = std::make_shared<PairedChannel>();
    auto b = std::make_shared<PairedChannel>();
    a->link(b);
    b->link(a);
    return {a, b};
}
