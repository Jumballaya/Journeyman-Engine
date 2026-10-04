#include "JobSystem.hpp"

JobSystem::JobSystem(size_t workerCount) : _threadPool(workerCount, 1024) {}

void JobSystem::execute(TaskGraph& graph) {
  bool workRemaining = true;

  while (workRemaining) {
    auto readyJobs = graph.fetchReadyJobs();
    for (auto& [id, job] : readyJobs) {
      submit(std::move(job));
    }
    waitForCompletion();
    workRemaining = !graph.isComplete();
  }
}

void JobSystem::submit(Job<>&& job) {
  _threadPool.enqueue(std::move(job));
}

void JobSystem::waitForCompletion() {
  _threadPool.waitForIdle();
}
