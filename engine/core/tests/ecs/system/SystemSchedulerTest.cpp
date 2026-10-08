#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "World.hpp"
#include "system/System.hpp"
#include "system/SystemScheduler.hpp"
#include "system/SystemTraits.hpp"
#include "system/TypeList.hpp"

namespace {

using Log = std::shared_ptr<std::vector<std::string>>;

// Appends its label (and the thread it ran on) to a shared log.
struct LoggingSystem : System {
  std::string label;
  Log log;
  std::shared_ptr<std::vector<std::thread::id>> threads;
  LoggingSystem(std::string l, Log lg, std::shared_ptr<std::vector<std::thread::id>> t = nullptr)
      : label(std::move(l)), log(std::move(lg)), threads(std::move(t)) {}
  void update(World&, float) override {
    log->push_back(label);
    if (threads) threads->push_back(std::this_thread::get_id());
  }
};

struct ProducerTag {};
struct Producer : LoggingSystem { using LoggingSystem::LoggingSystem; };
struct Consumer : LoggingSystem { using LoggingSystem::LoggingSystem; };
struct Logic : LoggingSystem { using LoggingSystem::LoggingSystem; };
struct Render : LoggingSystem { using LoggingSystem::LoggingSystem; };
struct Undeclared : LoggingSystem { using LoggingSystem::LoggingSystem; };

}  // namespace

template <> struct SystemTraits<Producer> {
  using Provides = TypeList<ProducerTag>; using DependsOn = EmptyList;
  using Reads = EmptyList; using Writes = EmptyList;
};
template <> struct SystemTraits<Consumer> {
  using Provides = EmptyList; using DependsOn = TypeList<ProducerTag>;
  using Reads = EmptyList; using Writes = EmptyList;
};
template <> struct SystemTraits<Logic> {
  using Provides = EmptyList; using DependsOn = EmptyList;
  using Reads = EmptyList; using Writes = EmptyList;
};
template <> struct SystemTraits<Render> {
  using Provides = EmptyList; using DependsOn = EmptyList;
  using Reads = EmptyList; using Writes = EmptyList;
  static constexpr SystemStage stage = SystemStage::Render;
};

// Every system runs once per frame, on the thread that runs the frame.
TEST(SystemScheduler, SystemsRunOnTheCallingThread) {
  World world;
  auto log = std::make_shared<std::vector<std::string>>();
  auto threads = std::make_shared<std::vector<std::thread::id>>();
  world.registerSystem<Logic>("a", log, threads);
  world.registerSystem<Render>("b", log, threads);

  world.runSystems(0.016f);

  ASSERT_EQ(log->size(), 2u);
  for (std::thread::id id : *threads) EXPECT_EQ(id, std::this_thread::get_id());
}

// DependsOn beats registration order: Consumer is registered first.
TEST(SystemScheduler, DependsOnTagRunsAfterProvider) {
  World world;
  auto log = std::make_shared<std::vector<std::string>>();
  world.registerSystem<Consumer>("consumer", log);
  world.registerSystem<Producer>("producer", log);

  world.runSystems(0.016f);

  EXPECT_EQ(*log, (std::vector<std::string>{"producer", "consumer"}));
}

// Stage beats registration order; an undeclared system is a Logic one.
TEST(SystemScheduler, StagesRunInOrder) {
  World world;
  auto log = std::make_shared<std::vector<std::string>>();
  world.registerSystem<Render>("render", log);
  world.registerSystem<Undeclared>("undeclared", log);
  world.registerSystem<Logic>("logic", log);

  world.runSystems(0.016f);

  EXPECT_EQ(*log, (std::vector<std::string>{"undeclared", "logic", "render"}));
}

// The same order every frame: a run can be repeated exactly.
TEST(SystemScheduler, OrderIsTheSameEveryFrame) {
  World world;
  auto log = std::make_shared<std::vector<std::string>>();
  world.registerSystem<Render>("render", log);
  world.registerSystem<Consumer>("consumer", log);
  world.registerSystem<Logic>("logic", log);
  world.registerSystem<Producer>("producer", log);

  world.runSystems(0.016f);
  const std::vector<std::string> first = *log;
  for (int frame = 0; frame < 50; ++frame) {
    log->clear();
    world.runSystems(0.016f);
    ASSERT_EQ(*log, first);
  }
}

// An edit preview runs only the render stage.
TEST(SystemScheduler, StagesBeforeFromAreSkipped) {
  World world;
  auto log = std::make_shared<std::vector<std::string>>();
  world.registerSystem<Logic>("logic", log);
  world.registerSystem<Render>("render", log);

  world.runSystems(0.016f, SystemStage::Render);

  EXPECT_EQ(*log, (std::vector<std::string>{"render"}));
}
