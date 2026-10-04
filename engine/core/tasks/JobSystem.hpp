#pragma once

#include "../async/Job.hpp"
#include "../async/LockFreeQueue.hpp"
#include "../async/ThreadPool.hpp"
#include "TaskGraph.hpp"
#include "TaskId.hpp"

// Runs a TaskGraph's jobs on a worker pool, wave by wave, until it completes.
class JobSystem {
 public:
  JobSystem(size_t workerCount = 4);
  ~JobSystem() = default;

  void execute(TaskGraph& graph);
  void submit(Job<>&& job);
  void waitForCompletion();

 private:
  ThreadPool _threadPool;
};