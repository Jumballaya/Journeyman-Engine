#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "../assets/TempDir.hpp"
#include "Application.hpp"
#include "EngineModule.hpp"
#include "ModuleRegistry.hpp"

namespace {

struct LifecycleLog {
  std::vector<int> initOrder;
  std::vector<int> shutdownOrder;
  std::vector<int> tickMainOrder;
};

class RecordingModule : public EngineModule {
 public:
  RecordingModule(LifecycleLog* log, int id) : _log(log), _id(id) {}

  void initialize(Application&) override { _log->initOrder.push_back(_id); }
  void shutdown(Application&) override { _log->shutdownOrder.push_back(_id); }
  void tickMainThread(Application&, float) override { _log->tickMainOrder.push_back(_id); }
  const char* name() const override { return "RecordingModule"; }

 private:
  LifecycleLog* _log;
  int _id;
};

// Application construction is somewhat heavy (spawns JobSystem worker threads,
// creates a wasm3 environment). Share a single instance across the suite;
// tests never call app.initialize(), so the app stays cheap.
class ModuleRegistryTest : public ::testing::Test {
 protected:
  static std::unique_ptr<TempDir> s_dir;
  static std::unique_ptr<Application> s_app;

  static void SetUpTestSuite() {
    s_dir = std::make_unique<TempDir>();
    s_app = std::make_unique<Application>(s_dir->path(), "fake-manifest.json");
  }

  static void TearDownTestSuite() {
    s_app.reset();
    s_dir.reset();
  }

  Application& app() { return *s_app; }
};

std::unique_ptr<TempDir> ModuleRegistryTest::s_dir;
std::unique_ptr<Application> ModuleRegistryTest::s_app;

}  // namespace

// initializeModules invokes each registered module's initialize().
TEST_F(ModuleRegistryTest, RegisterAndInitializeCallsInitialize) {
  LifecycleLog log;
  ModuleRegistry reg;
  reg.registerModule(std::make_unique<RecordingModule>(&log, 1));
  reg.registerModule(std::make_unique<RecordingModule>(&log, 2));

  reg.initializeModules(app());

  ASSERT_EQ(log.initOrder.size(), 2u);
  // Don't assert on shutdown yet — that's the next test.
  reg.shutdownModules(app());  // cleanup
}

// initialize() runs in registration order.
TEST_F(ModuleRegistryTest, InitializeInRegistrationOrder) {
  LifecycleLog log;
  ModuleRegistry reg;
  reg.registerModule(std::make_unique<RecordingModule>(&log, 1));
  reg.registerModule(std::make_unique<RecordingModule>(&log, 2));
  reg.registerModule(std::make_unique<RecordingModule>(&log, 3));

  reg.initializeModules(app());

  ASSERT_EQ(log.initOrder.size(), 3u);
  EXPECT_EQ(log.initOrder[0], 1);
  EXPECT_EQ(log.initOrder[1], 2);
  EXPECT_EQ(log.initOrder[2], 3);

  reg.shutdownModules(app());  // cleanup
}

// shutdown() runs in reverse registration order — the non-obvious contract.
TEST_F(ModuleRegistryTest, ShutdownInReverseRegistrationOrder) {
  LifecycleLog log;
  ModuleRegistry reg;
  reg.registerModule(std::make_unique<RecordingModule>(&log, 1));
  reg.registerModule(std::make_unique<RecordingModule>(&log, 2));
  reg.registerModule(std::make_unique<RecordingModule>(&log, 3));
  reg.initializeModules(app());

  reg.shutdownModules(app());

  ASSERT_EQ(log.shutdownOrder.size(), 3u);
  EXPECT_EQ(log.shutdownOrder[0], 3);
  EXPECT_EQ(log.shutdownOrder[1], 2);
  EXPECT_EQ(log.shutdownOrder[2], 1);
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
