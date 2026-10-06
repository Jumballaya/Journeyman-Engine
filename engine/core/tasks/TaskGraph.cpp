#include "TaskGraph.hpp"

void TaskGraph::addDependency(TaskId dependent, TaskId prerequisite) {
  assert(!_frozen && "addDependency called after graph began executing");
  _tasks.at(dependent).remainingDependencies.fetch_add(1, std::memory_order_relaxed);
  _tasks.at(prerequisite).dependents.push_back(dependent);
}

std::vector<Job<>> TaskGraph::fetchReadyJobs() {
  _frozen = true;
  std::vector<Job<>> ready;
  for (auto& [id, node] : _tasks) {
    if (node.job.valid() && node.remainingDependencies.load(std::memory_order_acquire) == 0) {
      ready.push_back(std::move(node.job));
    }
  }
  return ready;
}

// Runs on a worker; the graph's structure is frozen, so only the counters change.
void TaskGraph::onTaskComplete(TaskId id) {
  for (TaskId dependent : _tasks.at(id).dependents) {
    _tasks.at(dependent).remainingDependencies.fetch_sub(1, std::memory_order_acq_rel);
  }
  _remainingTasks.fetch_sub(1, std::memory_order_release);
}
