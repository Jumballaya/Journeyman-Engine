#pragma once

#include <atomic>
#include <cassert>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../async/Job.hpp"
#include "../async/LockFreeQueue.hpp"
#include "TaskId.hpp"

struct TaskNode {
  Job<> job;
  std::atomic<size_t> remainingDependencies = 0;
  std::vector<TaskId> dependents;
  bool submitted = false;
};

class TaskGraph {
 public:
  TaskGraph() = default;
  ~TaskGraph() = default;

  template <typename Fn>
  TaskId addTask(Fn&& func);
  void addDependency(TaskId dependent, TaskId prerequisite);
  std::vector<std::pair<TaskId, Job<>>> fetchReadyJobs();

  void onTaskComplete(TaskId id);
  bool isComplete() const;

 private:
  std::unordered_map<TaskId, TaskNode> _tasks;
  std::atomic<size_t> _remainingTasks{0};
  std::atomic<size_t> _nextId{0};
  std::atomic<bool> _frozen{false};
  TaskId generateTaskId();
};

template <typename Fn>
TaskId TaskGraph::addTask(Fn&& func) {
  assert(!_frozen.load(std::memory_order_acquire) &&
         "addTask called after graph began executing");

  TaskId taskId = generateTaskId();

  Job<> job;
  job.set([this, taskId, func = std::move(func)]() {
    func();
    onTaskComplete(taskId);
  });

  auto [it, inserted] = _tasks.try_emplace(taskId);
  it->second.job = std::move(job);

  _remainingTasks.fetch_add(1, std::memory_order_relaxed);

  return taskId;
}