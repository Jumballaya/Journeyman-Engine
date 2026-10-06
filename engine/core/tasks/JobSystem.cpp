#include "JobSystem.hpp"

void JobSystem::execute(TaskGraph& graph) {
  while (!graph.isComplete()) {
    auto ready = graph.fetchReadyJobs();
    if (ready.empty()) return;  // what's left waits on itself
    for (auto& job : ready) _threadPool.enqueue(std::move(job));
    _threadPool.waitForIdle();
  }
}
