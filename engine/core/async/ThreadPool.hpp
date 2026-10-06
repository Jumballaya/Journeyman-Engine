#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "./Job.hpp"
#include "./LockFreeQueue.hpp"

// Work-stealing pool over per-worker lock-free queues. Idle workers sleep on
// a condition variable (no busy spinning), so an idle engine costs ~0% CPU.
class ThreadPool {
 public:
  // At least one worker. Jobs still queued at destruction never run.
  explicit ThreadPool(std::size_t count, std::size_t queueCapacity = 1024);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <typename Fn>
  void enqueue(Fn&& fn) {
    Job job;
    job.set(std::forward<Fn>(fn));
    enqueue(std::move(job));
  }
  void enqueue(Job<>&& job);
  void waitForIdle();

 private:
  void work(std::size_t index);
  bool take(std::size_t index, Job<>& job);

  std::vector<std::thread> _threads;
  std::vector<std::unique_ptr<LockFreeQueue<Job<>>>> _queues;
  std::atomic<bool> _shutdown{false};
  std::atomic<size_t> _activeJobs{0};  // enqueued but not yet finished
  std::atomic<size_t> _queuedJobs{0};  // enqueued but not yet dequeued

  // Guards only sleep/wake: producers bump _queuedJobs before notifying and sleepers
  // re-check it under the lock, so wakeups aren't lost.
  std::mutex _wakeMutex;
  std::condition_variable _workAvailable;
  std::condition_variable _idle;
};
