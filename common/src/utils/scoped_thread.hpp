#pragma once

#include <thread>
#include <utility>

// ScopedThread — a std::thread wrapper that joins in its destructor.
// Use in place of bare std::thread members to guarantee the thread is
// joined (or at least joinable) when the owning object is destroyed,
// preventing std::terminate() and resource leaks.
class ScopedThread {
public:
    ScopedThread() = default;
    explicit ScopedThread(std::thread t) : t_(std::move(t)) {}

    ScopedThread(ScopedThread &&) = default;
    ScopedThread &operator=(ScopedThread &&) = default;

    // Assign from a bare std::thread (moves it in).
    ScopedThread &operator=(std::thread t) {
        if (t_.joinable())
            t_.join();
        t_ = std::move(t);
        return *this;
    }

    ~ScopedThread() {
        if (t_.joinable())
            t_.join();
    }

    // Forward std::thread interface for explicit join/detach.
    std::thread::id get_id() const { return t_.get_id(); }
    bool joinable() const { return t_.joinable(); }
    void join() { t_.join(); }
    void detach() { t_.detach(); }

private:
    std::thread t_;
};
