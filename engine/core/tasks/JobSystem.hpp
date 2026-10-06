#pragma once

#include "../async/ThreadPool.hpp"
#include "TaskGraph.hpp"

// Runs a TaskGraph's jobs on a worker pool, wave by wave, until it completes.
class JobSystem {
 public:
  JobSystem(size_t workerCount = 4) : _threadPool(workerCount) {}

  // Returns early, leaving the graph incomplete, if a dependency cycle stalls it.
  void execute(TaskGraph& graph);

 private:
  ThreadPool _threadPool;
};
