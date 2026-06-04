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

class ThreadPool {
public:
  explicit ThreadPool(
      std::size_t workerCount = std::thread::hardware_concurrency()) {
    if (workerCount == 0)
      workerCount = 1;

    for (std::size_t i = 0; i < workerCount; ++i) {
      workers_.emplace_back([this]() { workerLoop(); });
    }
  }

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      stopping_ = true;
    }
    queueCv_.notify_all();
    for (auto &worker : workers_) {
      if (worker.joinable())
        worker.join();
    }
  }

  template <typename Fn>
  auto submit(Fn &&fn) -> std::future<std::invoke_result_t<Fn>> {
    using Result = std::invoke_result_t<Fn>;
    auto task =
        std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
    std::future<Result> future = task->get_future();

    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      tasks_.emplace([task]() { (*task)(); });
    }

    queueCv_.notify_one();
    return future;
  }

private:
  void workerLoop() {
    while (true) {
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
