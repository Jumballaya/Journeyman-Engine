#include "ThreadPool.hpp"

#include <iostream>

ThreadPool::ThreadPool(std::size_t count, std::size_t queueCapacity) {
  _queues.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    _queues.emplace_back(std::make_unique<LockFreeQueue<Job<>>>(queueCapacity));
  }
  start(count);
}

ThreadPool::~ThreadPool() {
  stop();
}

void ThreadPool::start(std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    _threads.emplace_back([this, i] {
      while (_queues[i]->is_valid()) {
        Job job;
        if (_queues[i]->try_dequeue(job) || trySteal(i, job)) {
          try {
            job();
          } catch (const std::exception& e) {
            std::cerr << "[worker] job threw: " << e.what() << "\n";
          } catch (...) {
            std::cerr << "[worker] job threw unknown error\n";
          }
          _activeJobs.fetch_sub(1, std::memory_order_release);
        } else {
          std::this_thread::yield();
        }
      }
    });
  }
}

void ThreadPool::enqueue(Job<>&& job) {
  _activeJobs.fetch_add(1, std::memory_order_relaxed);
  size_t idx = getLeastLoadedQueue();
  while (!_queues[idx]->try_enqueue(std::move(job))) {
    std::this_thread::yield();
  }
}

void ThreadPool::waitForIdle() {
  while (_activeJobs.load(std::memory_order_acquire) > 0) {
    std::this_thread::yield();
  }
}

void ThreadPool::stop() {
  _shutdown.store(true);
  for (auto& queue : _queues) {
    queue->shutdown();
  }
  for (auto& t : _threads) {
    if (t.joinable()) t.join();
  }
  _threads.clear();
}

size_t ThreadPool::getLeastLoadedQueue() const {
  if (_queues.size() == 1) {
    return 0;
  }

  size_t idx = 0;
  size_t smallest = _queues[0]->size_approx();
  for (size_t i = 1; i < _queues.size(); ++i) {
    size_t sz = _queues[i]->size_approx();
    if (sz < smallest) {
      smallest = sz;
      idx = i;
    }
  }
  return idx;
}

bool ThreadPool::trySteal(size_t thiefId, Job<>& job) {
  for (size_t i = 0; i < _queues.size(); ++i) {
    if (i == thiefId) {
      continue;
    }
    if (_queues[i]->try_dequeue(job)) {
      return true;
    }
  }
  return false;
}