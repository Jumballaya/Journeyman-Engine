#include "ThreadPool.hpp"

#include <algorithm>
#include <iostream>

ThreadPool::ThreadPool(std::size_t count, std::size_t queueCapacity) {
  count = std::max<std::size_t>(count, 1);
  for (std::size_t i = 0; i < count; ++i) _queues.push_back(std::make_unique<LockFreeQueue<Job<>>>(queueCapacity));
  for (std::size_t i = 0; i < count; ++i) _threads.emplace_back([this, i] { work(i); });
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard lock(_wakeMutex);
    _shutdown.store(true, std::memory_order_release);
  }
  _workAvailable.notify_all();
  for (auto& t : _threads) t.join();
}

void ThreadPool::enqueue(Job<>&& job) {
  _activeJobs.fetch_add(1, std::memory_order_acq_rel);
  auto& queue = *std::min_element(_queues.begin(), _queues.end(),
                                  [](const auto& a, const auto& b) { return a->size_approx() < b->size_approx(); });
  while (!queue->try_enqueue(std::move(job))) std::this_thread::yield();
  _queuedJobs.fetch_add(1, std::memory_order_acq_rel);
  { std::lock_guard lock(_wakeMutex); }
  _workAvailable.notify_one();
}

void ThreadPool::waitForIdle() {
  std::unique_lock lock(_wakeMutex);
  _idle.wait(lock, [this] { return _activeJobs.load(std::memory_order_acquire) == 0; });
}

void ThreadPool::work(std::size_t index) {
  while (!_shutdown.load(std::memory_order_acquire)) {
    Job<> job;
    if (!take(index, job)) {
      std::unique_lock lock(_wakeMutex);
      _workAvailable.wait(lock, [this] {
        return _shutdown.load(std::memory_order_acquire) || _queuedJobs.load(std::memory_order_acquire) > 0;
      });
      continue;
    }
    _queuedJobs.fetch_sub(1, std::memory_order_acq_rel);
    try {
      job();
    } catch (const std::exception& e) {
      std::cerr << "[worker] job threw: " << e.what() << "\n";
    } catch (...) {
      std::cerr << "[worker] job threw unknown error\n";
    }
    if (_activeJobs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      { std::lock_guard lock(_wakeMutex); }
      _idle.notify_all();
    }
  }
}

// The worker's own queue first, then steal from the others.
bool ThreadPool::take(std::size_t index, Job<>& job) {
  for (std::size_t n = 0; n < _queues.size(); ++n) {
    if (_queues[(index + n) % _queues.size()]->try_dequeue(job)) return true;
  }
  return false;
}
