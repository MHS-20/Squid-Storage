#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/select.h>

#include "squidprotocol.hpp"

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t workerCount = std::thread::hardware_concurrency())
    {
        if (workerCount == 0)
            workerCount = 1;

        for (std::size_t i = 0; i < workerCount; ++i)
        {
            workers_.emplace_back([this]() { workerLoop(); });
        }
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stopping_ = true;
        }
        queueCv_.notify_all();
        for (auto &worker : workers_)
        {
            if (worker.joinable())
                worker.join();
        }
    }

    template <typename Fn>
    auto submit(Fn &&fn) -> std::future<std::invoke_result_t<Fn>>
    {
        using Result = std::invoke_result_t<Fn>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
        std::future<Result> future = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            tasks_.emplace([task]() { (*task)(); });
        }

        queueCv_.notify_one();
        return future;
    }

private:
    void workerLoop()
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCv_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty())
                    return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }

            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    bool stopping_ = false;
};

class ConnectionSession : public std::enable_shared_from_this<ConnectionSession>
{
public:
    using RequestHandler = std::function<void(ConnectionSession &, const Message &)>;

    ConnectionSession(std::shared_ptr<INetworkChannel> channel,
                      std::string nodeType,
                      std::string processName,
                      RequestHandler requestHandler = {})
        : channel_(std::move(channel)),
          protocol_(channel_, std::move(nodeType), std::move(processName)),
          requestHandler_(std::move(requestHandler))
    {
    }

    void start(bool readLoop)
    {
        readLoop_ = readLoop;
        alive_ = true;
        worker_ = std::thread([this]() { run(); });
    }

    void stop()
    {
        alive_ = false;
        if (channel_)
            channel_->close();
        queueCv_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }

    ~ConnectionSession()
    {
        stop();
    }

    bool isAlive() const { return alive_ && protocol_.isAlive(); }
    void setIsAlive(bool value) { alive_ = value; protocol_.setIsAlive(value); }
    std::string getProcessName() const { return protocol_.getProcessName(); }
    std::string getNodeType() const { return protocol_.getNodeType(); }
    std::string toString() const { return protocol_.toString(); }
    int socketFd() const { return protocol_.socketFd(); }

    template <typename Fn>
    auto call(Fn &&fn) -> std::invoke_result_t<Fn, SquidProtocol &>
    {
        using Result = std::invoke_result_t<Fn, SquidProtocol &>;
        if (std::this_thread::get_id() == ownerThreadId_)
            return std::forward<Fn>(fn)(protocol_);

        auto task = std::make_shared<std::packaged_task<Result()>>(
            [this, fn = std::forward<Fn>(fn)]() mutable -> Result {
                return fn(protocol_);
            });
        std::future<Result> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
                tasks_.emplace([task](SquidProtocol &) { (*task)(); });
        }
        queueCv_.notify_one();
        return future.get();
    }

    template <typename Fn>
    void post(Fn &&fn)
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
                tasks_.emplace([fn = std::forward<Fn>(fn)](SquidProtocol &protocol) mutable {
                fn(protocol);
            });
        }
        queueCv_.notify_one();
    }

    Message identify() { return call([](SquidProtocol &protocol) { return protocol.identify(); }); }
    Message connectServer() { return call([](SquidProtocol &protocol) { return protocol.connectServer(); }); }
    Message closeConn() { return call([](SquidProtocol &protocol) { return protocol.closeConn(); }); }
    Message listFiles() { return call([](SquidProtocol &protocol) { return protocol.listFiles(); }); }
    Message syncStatus() { return call([](SquidProtocol &protocol) { return protocol.syncStatus(); }); }

    Message createFile(const std::string &filePath)
    {
        return call([&](SquidProtocol &protocol) { return protocol.createFile(filePath); });
    }

    Message createFile(const std::string &filePath, int version)
    {
        return call([&](SquidProtocol &protocol) { return protocol.createFile(filePath, version); });
    }

    Message createFile(const std::string &filePath, int version, const std::vector<uint8_t> &fileData)
    {
        return call([&](SquidProtocol &protocol) { return protocol.createFile(filePath, version, fileData); });
    }

    Message readFile(const std::string &filePath)
    {
        return call([&](SquidProtocol &protocol) { return protocol.readFile(filePath); });
    }

    Message readFile(const std::string &filePath, std::vector<uint8_t> &fileData)
    {
        return call([&](SquidProtocol &protocol) { return protocol.readFile(filePath, fileData); });
    }

    Message updateFile(const std::string &filePath)
    {
        return call([&](SquidProtocol &protocol) { return protocol.updateFile(filePath); });
    }

    Message updateFile(const std::string &filePath, int version)
    {
        return call([&](SquidProtocol &protocol) { return protocol.updateFile(filePath, version); });
    }

    Message updateFile(const std::string &filePath, int version, const std::vector<uint8_t> &fileData)
    {
        return call([&](SquidProtocol &protocol) { return protocol.updateFile(filePath, version, fileData); });
    }

    Message deleteFile(const std::string &filePath)
    {
        return call([&](SquidProtocol &protocol) { return protocol.deleteFile(filePath); });
    }

    Message acquireLock(const std::string &filePath)
    {
        return call([&](SquidProtocol &protocol) { return protocol.acquireLock(filePath); });
    }

    Message releaseLock(const std::string &filePath)
    {
        return call([&](SquidProtocol &protocol) { return protocol.releaseLock(filePath); });
    }

    Message heartbeat()
    {
        return call([](SquidProtocol &protocol) { return protocol.heartbeat(); });
    }

    void response(bool isAck) { call([&](SquidProtocol &protocol) { protocol.response(isAck); return 0; }); }
    void response(int port) { call([&](SquidProtocol &protocol) { protocol.response(port); return 0; }); }
    void response(const std::string &ack) { call([&](SquidProtocol &protocol) { protocol.response(ack); return 0; }); }
    void response(const std::string &nodeType, const std::string &processName) { call([&](SquidProtocol &protocol) { protocol.response(nodeType, processName); return 0; }); }
    void response(const std::map<std::string, int> &fileVersionMap) { call([&](SquidProtocol &protocol) { protocol.response(fileVersionMap); return 0; }); }
    void response(const std::map<std::string, long long> &fileTimeMap) { call([&](SquidProtocol &protocol) { protocol.response(fileTimeMap); return 0; }); }
    void response(const std::map<std::string, fs::file_time_type> &filesLastWrite) { call([&](SquidProtocol &protocol) { protocol.response(filesLastWrite); return 0; }); }

    bool sendFileData(const std::vector<uint8_t> &fileData)
    {
        return call([&](SquidProtocol &protocol) { return protocol.sendFileData(fileData); });
    }

    bool receiveFileData(std::vector<uint8_t> &fileData)
    {
        return call([&](SquidProtocol &protocol) { return protocol.receiveFileData(fileData); });
    }

    void run()
    {
        ownerThreadId_ = std::this_thread::get_id();
        while (alive_ && protocol_.isAlive())
        {
            drainQueue();

            if (!readLoop_)
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return !alive_ || !tasks_.empty();
                });
                continue;
            }

            if (socketFd() < 0)
                break;

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(socketFd(), &readfds);

            struct timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;

            int ready = ::select(socketFd() + 1, &readfds, nullptr, nullptr, &timeout);
            if (ready < 0)
                continue;

            if (ready > 0 && FD_ISSET(socketFd(), &readfds))
            {
                Message mex = protocol_.receiveAndParse();
                if (!protocol_.isAlive())
                    break;
                if (requestHandler_)
                    requestHandler_(*this, mex);
            }
        }

        alive_ = false;
    }

private:
    void drainQueue()
    {
        while (true)
        {
            std::function<void(SquidProtocol &)> task;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (tasks_.empty())
                    break;
                task = std::move(tasks_.front());
                tasks_.pop();
            }

            task(protocol_);
        }
    }

    std::shared_ptr<INetworkChannel> channel_;
    SquidProtocol protocol_;
    RequestHandler requestHandler_;
    std::thread worker_;
    std::atomic<bool> alive_{false};
    bool readLoop_ = false;
    std::thread::id ownerThreadId_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::queue<std::function<void(SquidProtocol &)>> tasks_;
};