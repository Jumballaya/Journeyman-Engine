#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "JobSystem.hpp"
#include "TaskGraph.hpp"
#include "World.hpp"
#include "system/System.hpp"
#include "system/SystemScheduler.hpp"
#include "system/SystemTraits.hpp"
#include "system/TypeList.hpp"

namespace {

struct CountingSystem : System {
  std::shared_ptr<std::atomic<int>> counter;
  explicit CountingSystem(std::shared_ptr<std::atomic<int>> c) : counter(std::move(c)) {}
  void update(World&, float) override {
    counter->fetch_add(1, std::memory_order_relaxed);
  }
};

struct ProducerTag {};

struct ProducerSystem : System {
  std::shared_ptr<std::vector<std::string>> order;
  std::shared_ptr<std::mutex> m;
  ProducerSystem(std::shared_ptr<std::vector<std::string>> o,
                 std::shared_ptr<std::mutex> mu)
      : order(std::move(o)), m(std::move(mu)) {}
  void update(World&, float) override {
    std::lock_guard<std::mutex> lk(*m);
    order->push_back("producer");
  }
};

struct ConsumerSystem : System {
  std::shared_ptr<std::vector<std::string>> order;
  std::shared_ptr<std::mutex> m;
  ConsumerSystem(std::shared_ptr<std::vector<std::string>> o,
                 std::shared_ptr<std::mutex> mu)
      : order(std::move(o)), m(std::move(mu)) {}
  void update(World&, float) override {
    std::lock_guard<std::mutex> lk(*m);
    order->push_back("consumer");
  }
};

}  // namespace

template <>
struct SystemTraits<ProducerSystem> {
  using Provides = TypeList<ProducerTag>;
  using DependsOn = EmptyList;
  using Reads = EmptyList;
  using Writes = EmptyList;
};

template <>
struct SystemTraits<ConsumerSystem> {
  using Provides = EmptyList;
  using DependsOn = TypeList<ProducerTag>;
  using Reads = EmptyList;
  using Writes = EmptyList;
};

// A system registered on the World runs exactly once when the execution graph
// is built and then executed via the JobSystem.
TEST(SystemScheduler, RegisteredSystemsRunViaTaskGraph) {
  World world;
  auto counter = std::make_shared<std::atomic<int>>(0);
  world.registerSystem<CountingSystem>(counter);

  TaskGraph graph;
  world.buildExecutionGraph(graph, 0.016f);

  JobSystem js(2);
  js.execute(graph);

  EXPECT_EQ(counter->load(), 1);
}

// A system whose SystemTraits::DependsOn lists a tag runs AFTER the system
// whose SystemTraits::Provides lists that same tag. Consumer is registered
// first to rule out ordering-by-registration-accident.
TEST(SystemScheduler, DependsOnTagRunsAfterProvider) {
  World world;
  auto order = std::make_shared<std::vector<std::string>>();
  auto mutex = std::make_shared<std::mutex>();

  world.registerSystem<ConsumerSystem>(order, mutex);
  world.registerSystem<ProducerSystem>(order, mutex);

  TaskGraph graph;
  world.buildExecutionGraph(graph, 0.016f);

  JobSystem js(4);
  js.execute(graph);

  ASSERT_EQ(order->size(), 2u);
  EXPECT_EQ((*order)[0], "producer");
  EXPECT_EQ((*order)[1], "consumer");
}

// After SystemScheduler::clear(), a subsequent buildTaskGraph produces a
// graph that runs no previously-registered systems.
TEST(SystemScheduler, ClearRemovesSystems) {
  SystemScheduler scheduler;
  auto counter = std::make_shared<std::atomic<int>>(0);
  scheduler.registerSystem<CountingSystem>(counter);

  scheduler.clear();

  World world;
  TaskGraph graph;
  scheduler.buildTaskGraph(graph, world, 0.016f);

  JobSystem js(2);
  js.execute(graph);

  EXPECT_EQ(counter->load(), 0);
}
