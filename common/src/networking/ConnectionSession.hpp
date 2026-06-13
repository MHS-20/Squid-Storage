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

class ConnectionSession
    : public std::enable_shared_from_this<ConnectionSession> {
public:
  std::atomic<bool> readSuspended_{false};

  using RequestHandler =
      std::function<void(ConnectionSession &, const Message &)>;

  ConnectionSession(FileManager &fileManager,
                    std::shared_ptr<INetworkChannel> channel,
                    std::string nodeType, std::string processName,
                    RequestHandler requestHandler = {})
      : channel_(std::move(channel)),
        protocol_(fileManager, channel_, std::move(nodeType),
                  std::move(processName)),
        requestHandler_(std::move(requestHandler)) {}

  void start(bool readLoop) {
    readLoop_ = readLoop;
    alive_ = true;
    // ownerThreadId_ is set inside run() before any task can execute, but we
    // need it visible before any external call() could block on future.get().
    // The worker sets it as the very first thing, protected by the fact that
    // call() from outside will only submit a task and block — it cannot run
    // before ownerThreadId_ is written because the worker hasn't started yet.
    worker_ = std::thread([this]() { run(); });
  }

  void stop() {
    alive_ = false;
    if (channel_)
      channel_->close();
    queueCv_.notify_all();
    // If stop() is called from the worker thread itself (e.g. the destructor
    // fires because the last shared_ptr was released inside a requestHandler
    // callback), joining would deadlock. Detach so the thread cleans itself up.
    if (worker_.joinable()) {
      if (std::this_thread::get_id() == ownerThreadId_.load())
        worker_.detach();
      else
        worker_.join();
    }
  }

  ~ConnectionSession() { stop(); }

  bool isAlive() const { return alive_ && protocol_.isAlive(); }
  void setIsAlive(bool value) {
    alive_ = value;
    protocol_.setIsAlive(value);
  }

  std::string getProcessName() const { return protocol_.getProcessName(); }
  std::string getNodeType() const { return protocol_.getNodeType(); }
  std::string toString() const { return protocol_.toString(); }
  int socketFd() const { return protocol_.socketFd(); }

  // call() dispatches fn to the session worker thread and blocks until done.
  // If already on the worker thread (e.g. inside a requestHandler callback),
  // fn is called directly to avoid deadlock.
  template <typename Fn>
  auto call(Fn &&fn) -> std::invoke_result_t<Fn, SquidProtocol &> {
    using Result = std::invoke_result_t<Fn, SquidProtocol &>;
    if (std::this_thread::get_id() == ownerThreadId_.load())
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

  // post() enqueues fn for asynchronous execution on the worker thread.
  // Use for fire-and-forget operations (e.g. server-initiated pushes).
  // Captures fn by value — never capture caller stack variables by reference.
  template <typename Fn> void post(Fn &&fn) {
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      tasks_.emplace([fn = std::forward<Fn>(fn)](
                         SquidProtocol &protocol) mutable { fn(protocol); });
    }
    queueCv_.notify_one();
  }

  void responseDispatcher(const Message &msg) {
    call([msg](SquidProtocol &protocol) {
      protocol.responseDispatcher(msg);
      return 0;
    });
  }

  Message identify() {
    return call([](SquidProtocol &protocol) { return protocol.identify(); });
  }
  Message connectServer() {
    return call(
        [](SquidProtocol &protocol) { return protocol.connectServer(); });
  }
  Message closeConn() {
    return call([](SquidProtocol &protocol) { return protocol.closeConn(); });
  }
  Message syncStatus() {
    return call([](SquidProtocol &protocol) { return protocol.syncStatus(); });
  }

  Message createFile(const std::string &filePath) {
    return call([filePath](SquidProtocol &protocol) {
      return protocol.createFile(filePath);
    });
  }

  Message createFile(const std::string &filePath, int version) {
    return call([filePath, version](SquidProtocol &protocol) {
      return protocol.createFile(filePath, version);
    });
  }

  Message createFile(const std::string &filePath, int version,
                     const std::vector<uint8_t> &fileData) {
    return call([filePath, version, fileData](SquidProtocol &protocol) {
      return protocol.createFile(filePath, version, fileData);
    });
  }

  Message readFile(const std::string &filePath,
                   std::vector<uint8_t> &fileData) {
    // fileData is an out-parameter: capture by reference is safe here because
    // call() blocks until the task completes, so the caller's stack is live.
    return call([filePath, &fileData](SquidProtocol &protocol) {
      return protocol.readFile(filePath, fileData);
    });
  }

  Message updateFile(const std::string &filePath) {
    return call([filePath](SquidProtocol &protocol) {
      return protocol.updateFile(filePath);
    });
  }

  Message updateFile(const std::string &filePath, int version) {
    return call([filePath, version](SquidProtocol &protocol) {
      return protocol.updateFile(filePath, version);
    });
  }

  Message updateFile(const std::string &filePath, int version,
                     const std::vector<uint8_t> &fileData) {
    return call([filePath, version, fileData](SquidProtocol &protocol) {
      return protocol.updateFile(filePath, version, fileData);
    });
  }

  Message deleteFile(const std::string &filePath) {
    return call([filePath](SquidProtocol &protocol) {
      return protocol.deleteFile(filePath);
    });
  }

  Message acquireLock(const std::string &filePath) {
    return call([filePath](SquidProtocol &protocol) {
      return protocol.acquireLock(filePath);
    });
  }

  Message releaseLock(const std::string &filePath) {
    return call([filePath](SquidProtocol &protocol) {
      return protocol.releaseLock(filePath);
    });
  }

  Message heartbeat() {
    return call([](SquidProtocol &protocol) { return protocol.heartbeat(); });
  }

  void response(bool isAck) {
    call([isAck](SquidProtocol &protocol) {
      protocol.response(isAck);
      return 0;
    });
  }
  void response(bool isAck, int version) {
    call([isAck, version](SquidProtocol &protocol) {
      protocol.response(isAck, version);
      return 0;
    });
  }
  void response(int port) {
    call([port](SquidProtocol &protocol) {
      protocol.response(port);
      return 0;
    });
  }
  void response(const std::string &ack) {
    call([ack](SquidProtocol &protocol) {
      protocol.response(ack);
      return 0;
    });
  }
  void response(const std::string &nodeType, const std::string &processName) {
    call([nodeType, processName](SquidProtocol &protocol) {
      protocol.response(nodeType, processName);
      return 0;
    });
  }
  void response(const std::map<std::string, int> &fileVersionMap) {
    call([fileVersionMap](SquidProtocol &protocol) {
      protocol.response(fileVersionMap);
      return 0;
    });
  }
  void response(const std::map<std::string, long long> &fileTimeMap) {
    call([fileTimeMap](SquidProtocol &protocol) {
      protocol.response(fileTimeMap);
      return 0;
    });
  }

  void suspendReads() { readSuspended_.store(true); }
  void resumeReads() { readSuspended_.store(false); }

  void
  response(const std::map<std::string, fs::file_time_type> &filesLastWrite) {
    call([filesLastWrite](SquidProtocol &protocol) {
      protocol.response(filesLastWrite);
      return 0;
    });
  }

  bool sendFileData(const std::vector<uint8_t> &fileData) {
    return call([fileData](SquidProtocol &protocol) {
      return protocol.sendFileData(fileData);
    });
  }

  bool receiveFileData(std::vector<uint8_t> &fileData) {
    // Out-parameter: capture by reference is safe because call() blocks.
    return call([&fileData](SquidProtocol &protocol) {
      return protocol.receiveFileData(fileData);
    });
  }

  // Push helpers — use PUSH_* opcodes so the receiver can distinguish them
  // from request/response frames unambiguously.
  void pushCreateFile(const std::string &filePath, int version,
                      const std::vector<uint8_t> &fileData) {
    post([filePath, version, fileData](SquidProtocol &protocol) {
      protocol.pushCreateFile(filePath, version, fileData);
    });
  }

  void pushUpdateFile(const std::string &filePath, int version,
                      const std::vector<uint8_t> &fileData) {
    post([filePath, version, fileData](SquidProtocol &protocol) {
      protocol.pushUpdateFile(filePath, version, fileData);
    });
  }

  void pushDeleteFile(const std::string &filePath) {
    post([filePath](SquidProtocol &protocol) {
      protocol.pushDeleteFile(filePath);
    });
  }

  void run() {
    // Set ownerThreadId_ as the very first thing so that any call() arriving
    // while the loop is already running takes the direct path correctly.
    ownerThreadId_.store(std::this_thread::get_id());

    while (alive_ && protocol_.isAlive()) {
      drainQueue();

      if (!readLoop_) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCv_.wait_for(lock, std::chrono::milliseconds(100),
                          [this]() { return !alive_ || !tasks_.empty(); });
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

      int ready =
          ::select(socketFd() + 1, &readfds, nullptr, nullptr, &timeout);
      if (ready < 0)
        continue;

      if (ready > 0 && FD_ISSET(socketFd(), &readfds)) {
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
  void drainQueue() {
    while (true) {
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
  // Stored atomically so call() can read it from any thread without a lock.
  std::atomic<std::thread::id> ownerThreadId_;
  std::mutex queueMutex_;
  std::condition_variable queueCv_;
  std::queue<std::function<void(SquidProtocol &)>> tasks_;
};
