#pragma once

#include <atomic>
#include <cassert>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../async/Job.hpp"
#include "TaskId.hpp"

// A single-use DAG of jobs: add tasks and dependencies, then JobSystem::execute
// runs it. The first fetchReadyJobs freezes it.
class TaskGraph {
 public:
  template <typename Fn>
  TaskId addTask(Fn&& func);
  void addDependency(TaskId dependent, TaskId prerequisite);

  // The not-yet-fetched jobs whose prerequisites have all completed.
  std::vector<Job<>> fetchReadyJobs();
  bool isComplete() const { return _remainingTasks.load(std::memory_order_acquire) == 0; }

 private:
  struct Node {
    Job<> job;  // empty once fetched
    std::atomic<size_t> remainingDependencies = 0;
    std::vector<TaskId> dependents;
  };

  void onTaskComplete(TaskId id);

  std::unordered_map<TaskId, Node> _tasks;
  std::atomic<size_t> _remainingTasks{0};
  size_t _nextId = 0;
  bool _frozen = false;
};

template <typename Fn>
TaskId TaskGraph::addTask(Fn&& func) {
  assert(!_frozen && "addTask called after graph began executing");
  const TaskId id{_nextId++};
  _tasks[id].job.set([this, id, func = std::forward<Fn>(func)]() {
    // Complete even if the task throws, or the graph would never finish.
    struct Completion {
      TaskGraph* graph;
      TaskId id;
      ~Completion() { graph->onTaskComplete(id); }
    } completion{this, id};
    func();
  });
  _remainingTasks.fetch_add(1, std::memory_order_relaxed);
  return id;
}
