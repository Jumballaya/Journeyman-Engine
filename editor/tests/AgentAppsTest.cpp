#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "AgentApps.hpp"

namespace fs = std::filesystem;

TEST(AgentApps, ReadsDoctorsAgentsAndSkipsWhatItCantRead) {
  const auto apps = AgentApps::parse(
      "note\n{\"ok\": false, \"agents\": [{\"name\": \"codex\", \"title\": \"Codex\", \"status\": \"connected\"},"
      " {\"name\": \"chatgpt\", \"title\": \"ChatGPT\", \"status\": \"manual\"}, {\"status\": \"connected\"}, 3]}");
  ASSERT_TRUE(apps);
  ASSERT_EQ(apps->size(), 2u);
  EXPECT_EQ((*apps)[0].title, "Codex");
  EXPECT_EQ((*apps)[0].status, AgentApps::Status::Connected);
  EXPECT_EQ((*apps)[1].status, AgentApps::Status::Manual);
  EXPECT_FALSE(AgentApps::parse("jm: command not found"));
}

#ifndef _WIN32
namespace {

void waitFor(AgentApps& agents) {
  for (int i = 0; i < 500 && agents.busy(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT_FALSE(agents.busy());
}

AgentApps::Status codexStatus(AgentApps& agents) { return agents.apps()->at(0).status; }

}  // namespace

// Connect runs jm setup, says what it changed, and the app then needs a restart.
TEST(AgentApps, ConnectingRunsSetupThenAsksAgain) {
  const fs::path dir = fs::temp_directory_path() / "jm-agent-apps-test";
  fs::create_directories(dir);
  const fs::path jm = dir / "jm", state = dir / "state";
  std::ofstream(state) << "not-connected";
  std::ofstream(jm) << "#!/bin/sh\n"
                       "if [ \"$1\" = doctor ]; then\n"
                       "  echo '{\"agents\": [{\"name\": \"codex\", \"title\": \"Codex\", \"status\": \"'$(cat " + state.string() + ")'\"}]}'\n"
                       "  exit 1\n"  // doctor exits 1 on any problem
                       "fi\n"
                       "if [ \"$2\" = --remove ]; then echo not-connected > " + state.string() + "; echo \"codex  removed journeyman\"; exit 0; fi\n"
                       "echo connected > " + state.string() + "\n"
                       "echo \"codex  added journeyman to /x/config.toml\"\n";
  fs::permissions(jm, fs::perms::owner_all);
  setenv("JM_CLI", jm.c_str(), 1);

  AgentApps agents;
  EXPECT_FALSE(agents.apps());  // asking starts now
  waitFor(agents);
  ASSERT_TRUE(agents.apps());
  EXPECT_EQ(codexStatus(agents), AgentApps::Status::NotConnected);
  EXPECT_FALSE(agents.apps()->at(0).needsRestart);

  agents.connect("codex", true);
  waitFor(agents);
  EXPECT_EQ(codexStatus(agents), AgentApps::Status::Connected);
  EXPECT_TRUE(agents.apps()->at(0).needsRestart);
  ASSERT_TRUE(agents.lastChange());
  EXPECT_TRUE(agents.lastChange()->ok);
  EXPECT_EQ(agents.lastChange()->lines, std::vector<std::string>{"codex  added journeyman to /x/config.toml"});

  agents.connect("codex", false);
  waitFor(agents);
  EXPECT_EQ(codexStatus(agents), AgentApps::Status::NotConnected);
  unsetenv("JM_CLI");
  fs::remove_all(dir);
}
#endif
