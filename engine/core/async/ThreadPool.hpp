#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "./Job.hpp"
#include "./LockFreeQueue.hpp"

// Work-stealing pool over per-worker lock-free queues. Idle workers sleep on
// a condition variable (no busy spinning), so an idle engine costs ~0% CPU.
class ThreadPool {
 public:
  explicit ThreadPool(std::size_t count, std::size_t queueCapacity = 1024);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <typename Fn>
  void enqueue(Fn&& fn);
  void enqueue(Job<>&& job);
  void waitForIdle();

 private:
  void start(std::size_t count);
  void stop();
  void push(Job<>&& job);
  void finishJob();

  std::vector<std::thread> _threads;
  std::vector<std::unique_ptr<LockFreeQueue<Job<>>>> _queues;
  std::atomic<bool> _shutdown{false};
  std::atomic<size_t> _activeJobs{0};  // enqueued but not yet finished
  std::atomic<size_t> _queuedJobs{0};  // enqueued but not yet dequeued

  // _wakeMutex guards the sleep/wake handshake only; the queues stay lock-free.
  // Producers bump _queuedJobs BEFORE taking the mutex to notify, and sleepers
  // re-check it under the mutex, so a wakeup can't be lost.
  std::mutex _wakeMutex;
  std::condition_variable _workAvailable;
  std::condition_variable _idle;

  size_t getLeastLoadedQueue() const;
  bool trySteal(size_t thiefId, Job<>& job);
};

template <typename Fn>
void ThreadPool::enqueue(Fn&& fn) {
  Job job;
  job.set(std::forward<Fn>(fn));
  push(std::move(job));
}