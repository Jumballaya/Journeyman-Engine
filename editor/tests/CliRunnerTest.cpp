#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "CliRunner.hpp"
#include "LogBook.hpp"

namespace fs = std::filesystem;

TEST(LogBook, ANewBuildRetiresTheLastOnesProblems) {
  LogBook book;
  book.add(LogBook::Level::Warning, LogBook::Source::Build, "warning: scenes/main.scene.json: not valid JSON");
  book.add(LogBook::Level::Error, LogBook::Source::Engine, "[Script] boom");
  book.retireBuildProblems();
  EXPECT_EQ(book.counts()[static_cast<int>(LogBook::Level::Warning)], 0);
  EXPECT_EQ(book.counts()[static_cast<int>(LogBook::Level::Error)], 1);  // the game's, not a build's
  // Still there in the new build: it counts again (not folded into the retired line).
  book.add(LogBook::Level::Warning, LogBook::Source::Build, "warning: scenes/main.scene.json: not valid JSON");
  EXPECT_EQ(book.counts()[static_cast<int>(LogBook::Level::Warning)], 1);
}

#ifndef _WIN32
// asc names an error's place on the next line, relative to assets/scripts/.
TEST(CliRunner, AFailedBuildReportsItsFirstErrorAndWhereItIs) {
  const fs::path dir = fs::temp_directory_path() / "jm-cli-runner-test";
  fs::create_directories(dir);
  const fs::path jm = dir / "jm";
  std::ofstream(jm) << "#!/bin/sh\n"
                       "echo 'ERROR TS2304: Cannot find name nope.'\n"
                       "echo '   \xE2\x94\x94\xE2\x94\x80 in ../player.ts(10,21)'\n"
                       "echo 'ERROR TS1005: later'\n"
                       "echo '   \xE2\x94\x94\xE2\x94\x80 in lib/db.ts(3,1)'\n"
                       "echo 'Copied asset: assets/tiles.tsj'\n"
                       "exit 1\n";
  fs::permissions(jm, fs::perms::owner_all);
  setenv("JM_CLI", jm.c_str(), 1);

  CliRunner runner;
  ASSERT_TRUE(runner.start(dir, {"build"}, "Build"));
  std::optional<CliRunner::Finished> done;
  for (int i = 0; i < 1000 && !done; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    done = runner.takeFinished();
  }
  ASSERT_TRUE(done);
  EXPECT_FALSE(done->ok);
  EXPECT_EQ(done->message, "ERROR TS2304: Cannot find name nope.");
  EXPECT_EQ(done->file, "assets/player.ts");
  EXPECT_EQ(done->line, 10);
  unsetenv("JM_CLI");
  fs::remove_all(dir);
}
#endif
