#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

#include "../assets/TempDir.hpp"
#include "Engine.hpp"
#include "EngineModule.hpp"
#include "ModuleRegistry.hpp"
#include "ModuleTraits.hpp"

namespace {

struct LifecycleLog {
  std::vector<int> initOrder;
  std::vector<int> shutdownOrder;
  std::vector<int> tickMainOrder;
};

// Generic module that records its init/shutdown/tick events, parameterized by
// a label and a back-pointer to a log. Used as the base for all test modules.
class RecordingModule : public EngineModule {
 public:
  RecordingModule(LifecycleLog* log, int id) : _log(log), _id(id) {}

  void initialize(Engine&) override { _log->initOrder.push_back(_id); }
  void shutdown(Engine&) override { _log->shutdownOrder.push_back(_id); }
  void tickMainThread(Engine&, float) override { _log->tickMainOrder.push_back(_id); }
  const char* name() const override { return "RecordingModule"; }

 private:
  LifecycleLog* _log;
  int _id;
};

// Test tag types used to wire ModuleTraits specializations below.
struct TestTagX {};
struct TestTagY {};

// Provider modules provide a tag; they're distinguished by type so that
// ModuleTraits can be specialized on them.
struct ProviderXModule : RecordingModule {
  using RecordingModule::RecordingModule;
};
struct ConsumerXModule : RecordingModule {
  using RecordingModule::RecordingModule;
};
struct CycleAModule : RecordingModule {
  using RecordingModule::RecordingModule;
};
struct CycleBModule : RecordingModule {
  using RecordingModule::RecordingModule;
};

}  // namespace

// ModuleTraits specializations have to be at the global namespace because
// they extend the primary template declared there.
template <>
struct ModuleTraits<ProviderXModule> {
  using Provides = TypeList<TestTagX>;
  using DependsOn = TypeList<>;
};
template <>
struct ModuleTraits<ConsumerXModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<TestTagX>;
};
// CycleA provides TagX, depends on TagY.
template <>
struct ModuleTraits<CycleAModule> {
  using Provides = TypeList<TestTagX>;
  using DependsOn = TypeList<TestTagY>;
};
// CycleB provides TagY, depends on TagX — forming a cycle with CycleA.
template <>
struct ModuleTraits<CycleBModule> {
  using Provides = TypeList<TestTagY>;
  using DependsOn = TypeList<TestTagX>;
};

namespace {

// Engine construction is somewhat heavy (spawns JobSystem worker threads,
// creates a wasm3 environment). Share a single instance across the suite;
// tests never call app.initialize(), so the app stays cheap.
class ModuleRegistryTest : public ::testing::Test {
 protected:
  static std::unique_ptr<TempDir> s_dir;
  static std::unique_ptr<Engine> s_app;

  static void SetUpTestSuite() {
    s_dir = std::make_unique<TempDir>();
    s_app = std::make_unique<Engine>(s_dir->path(), "fake-manifest.json");
  }

  static void TearDownTestSuite() {
    s_app.reset();
    s_dir.reset();
  }

  Engine& app() { return *s_app; }
};

std::unique_ptr<TempDir> ModuleRegistryTest::s_dir;
std::unique_ptr<Engine> ModuleRegistryTest::s_app;

}  // namespace

// initializeModules invokes each registered module's initialize().
TEST_F(ModuleRegistryTest, RegisterAndInitializeCallsInitialize) {
  LifecycleLog log;
  ModuleRegistry reg;
  reg.registerModule(std::make_unique<RecordingModule>(&log, 1));
  reg.registerModule(std::make_unique<RecordingModule>(&log, 2));

  reg.initializeModules(app());

  ASSERT_EQ(log.initOrder.size(), 2u);
  reg.shutdownModules(app());  // cleanup
}

// When module B depends on a tag that module A provides, A initializes before
// B regardless of registration order.
TEST_F(ModuleRegistryTest, InitializeRespectsDependencyOrder) {
  LifecycleLog log;
  ModuleRegistry reg;
  // Register consumer FIRST so a naive "vector order" implementation would
  // fail — only dependency-order handling gives us Provider → Consumer.
  reg.registerModule<ConsumerXModule>(&log, /*id=*/2);
  reg.registerModule<ProviderXModule>(&log, /*id=*/1);

  reg.initializeModules(app());

  ASSERT_EQ(log.initOrder.size(), 2u);
  EXPECT_EQ(log.initOrder[0], 1);  // provider first
  EXPECT_EQ(log.initOrder[1], 2);  // then consumer

  reg.shutdownModules(app());  // cleanup
}

// Shutdown runs in reverse dependency order — consumer shuts down before the
// provider whose tag it depended on.
TEST_F(ModuleRegistryTest, ShutdownInReverseDependencyOrder) {
  LifecycleLog log;
  ModuleRegistry reg;
  reg.registerModule<ConsumerXModule>(&log, /*id=*/2);
  reg.registerModule<ProviderXModule>(&log, /*id=*/1);
  reg.initializeModules(app());

  reg.shutdownModules(app());

  ASSERT_EQ(log.shutdownOrder.size(), 2u);
  EXPECT_EQ(log.shutdownOrder[0], 2);  // consumer first
  EXPECT_EQ(log.shutdownOrder[1], 1);  // then provider
}

// Two modules with mutual dependencies throw on initializeModules rather than
// silently dropping work or hanging.
TEST_F(ModuleRegistryTest, CyclicDependenciesThrow) {
  LifecycleLog log;
  ModuleRegistry reg;
  reg.registerModule<CycleAModule>(&log, /*id=*/1);
  reg.registerModule<CycleBModule>(&log, /*id=*/2);

  EXPECT_THROW(reg.initializeModules(app()), std::runtime_error);
}

// After shutdownModules(), the registry is empty — subsequent tickMainThread
// is a no-op, not a crash or a re-invocation.
TEST_F(ModuleRegistryTest, ShutdownClearsRegistry) {
  LifecycleLog log;
  ModuleRegistry reg;
  reg.registerModule(std::make_unique<RecordingModule>(&log, 1));
  reg.initializeModules(app());
  reg.shutdownModules(app());

  reg.tickMainThreadModules(app(), 0.016f);

  EXPECT_TRUE(log.tickMainOrder.empty());
}
