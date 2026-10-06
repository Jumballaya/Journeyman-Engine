#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
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

namespace {
struct CompA : Component<CompA> { COMPONENT_NAME("CompA"); };
struct CompB : Component<CompB> { COMPONENT_NAME("CompB"); };

// Records start/end into a shared log so tests can detect overlap.
struct TracingSystem : System {
  std::string label;
  std::shared_ptr<std::vector<std::string>> log;
  std::shared_ptr<std::mutex> m;
  TracingSystem(std::string l, std::shared_ptr<std::vector<std::string>> lg, std::shared_ptr<std::mutex> mu)
      : label(std::move(l)), log(std::move(lg)), m(std::move(mu)) {}
  void update(World&, float) override {
    { std::lock_guard lk(*m); log->push_back(label + "+"); }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    { std::lock_guard lk(*m); log->push_back(label + "-"); }
  }
};
struct WriterA : TracingSystem { using TracingSystem::TracingSystem; };
struct ReaderA : TracingSystem { using TracingSystem::TracingSystem; };
struct RenderStage : TracingSystem { using TracingSystem::TracingSystem; };
struct Undeclared : TracingSystem { using TracingSystem::TracingSystem; };
}  // namespace

template <> struct SystemTraits<WriterA> {
  using Provides = EmptyList; using DependsOn = EmptyList;
  using Reads = EmptyList; using Writes = TypeList<CompA>;
};
template <> struct SystemTraits<ReaderA> {
  using Provides = EmptyList; using DependsOn = EmptyList;
  using Reads = TypeList<CompA>; using Writes = EmptyList;
};
template <> struct SystemTraits<RenderStage> {
  using Provides = EmptyList; using DependsOn = EmptyList;
  using Reads = TypeList<CompA>; using Writes = EmptyList;
  static constexpr SystemStage stage = SystemStage::Render;
};

namespace {
bool sequential(const std::vector<std::string>& log, const std::string& first, const std::string& second) {
  auto pos = [&](const std::string& s) { return std::find(log.begin(), log.end(), s) - log.begin(); };
  return pos(first + "-") < pos(second + "+");
}
}  // namespace

// A writer and a reader of the same component never overlap, and run in
// registration order within a stage.
TEST(SystemScheduler, ConflictingSystemsAreSerialized) {
  World world;
  auto log = std::make_shared<std::vector<std::string>>();
  auto m = std::make_shared<std::mutex>();
  world.registerSystem<WriterA>("w", log, m);
  world.registerSystem<ReaderA>("r", log, m);

  TaskGraph graph;
  world.buildExecutionGraph(graph, 0.016f);
  JobSystem js(4);
  js.execute(graph);

  ASSERT_EQ(log->size(), 4u);
  EXPECT_TRUE(sequential(*log, "w", "r"));
}

// Stage beats registration order: a Render-stage reader registered first
// still runs after a Logic-stage writer.
TEST(SystemScheduler, StageOrdersConflictingSystems) {
  World world;
  auto log = std::make_shared<std::vector<std::string>>();
  auto m = std::make_shared<std::mutex>();
  world.registerSystem<RenderStage>("render", log, m);
  world.registerSystem<WriterA>("w", log, m);

  TaskGraph graph;
  world.buildExecutionGraph(graph, 0.016f);
  JobSystem js(4);
  js.execute(graph);

  EXPECT_TRUE(sequential(*log, "w", "render"));
}

// A system with no SystemTraits specialization is exclusive: it never runs
// concurrently with anything, even systems that declare disjoint access.
TEST(SystemScheduler, UndeclaredSystemIsExclusive) {
  World world;
  auto log = std::make_shared<std::vector<std::string>>();
  auto m = std::make_shared<std::mutex>();
  world.registerSystem<Undeclared>("u", log, m);
  world.registerSystem<ReaderA>("r", log, m);

  TaskGraph graph;
  world.buildExecutionGraph(graph, 0.016f);
  JobSystem js(4);
  js.execute(graph);

  EXPECT_TRUE(sequential(*log, "u", "r"));
}
