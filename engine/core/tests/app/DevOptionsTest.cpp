#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "DevOptions.hpp"

// Both builds' DevOptions::fromEnvironment, under the same agent switches: the
// engine jm runs reads them (test_engine_core), the exported game's player
// reads none (test_engine_player links engine_dev_options_player).
namespace {
void setEnv(const char* name, const char* value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

DevOptions underAgentSwitches() {
  for (const char* name : {"JM_HEADLESS", "JM_DRIVE", "JM_STRICT", "JM_WATCH", "JM_DEBUG_DRAW", "JM_NET_HOST"}) setEnv(name, "1");
  setEnv("JM_EXIT_AFTER_FRAMES", "5");
  setEnv("JM_DUMP_DIR", "dump");
  setEnv("JM_CONTROL", "control");
  setEnv("JM_RECORD_DIR", "record");
  setEnv("JM_PROFILE", "profile.json");
  setEnv("JM_NET_LATENCY", "100");
  return DevOptions::fromEnvironment();
}
}  // namespace

#ifdef JM_PLAYER_BUILD
TEST(DevOptions, ThePlayerReadsNoSwitch) {
  const DevOptions o = underAgentSwitches();
  EXPECT_FALSE(o.headless || o.drive || o.strict || o.watch || o.automated());
  EXPECT_EQ(o.exitAfterFrames, 0u);
  EXPECT_FLOAT_EQ(o.fixedDt, 0.0f);
  EXPECT_FALSE(o.seed);
  EXPECT_TRUE(o.dumpDir.empty() && o.control.empty() && o.recordDir.empty() && o.profileOut.empty());
  EXPECT_TRUE(o.netHost.empty());
  EXPECT_FLOAT_EQ(o.netLatency, 0.0f);
  EXPECT_EQ(o.debugDraw, false);  // Debug.* calls draw nothing
}
#else
TEST(DevOptions, TheEngineReadsTheSwitches) {
  const DevOptions o = underAgentSwitches();
  EXPECT_TRUE(o.headless && o.drive && o.strict && o.watch && o.automated());
  EXPECT_EQ(o.exitAfterFrames, 5u);
  EXPECT_EQ(o.dumpDir, "dump");
  EXPECT_EQ(o.control, "control");
  EXPECT_EQ(o.netHost, "1");
  EXPECT_FLOAT_EQ(o.netLatency, 100.0f);
  EXPECT_EQ(o.debugDraw, true);
}
#endif
