#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "Shell.hpp"

TEST(Shell, PosixQuotesAnything) {
  EXPECT_EQ(shell::quotePosix("plain"), "'plain'");
  EXPECT_EQ(shell::quotePosix("it's"), "'it'\\''s'");
  EXPECT_EQ(shell::quotePosix("$HOME; rm -rf /"), "'$HOME; rm -rf /'");
}

// What Windows programs (and Go's os.Args) read back is the argument itself.
TEST(Shell, WindowsQuotesForTheCRuntime) {
  EXPECT_EQ(shell::quoteWindows("C:\\Games\\My Game"), "\"C:\\Games\\My Game\"");
  EXPECT_EQ(shell::quoteWindows("say \"hi\""), "\"say \"\"hi\"\"\"");
  EXPECT_EQ(shell::quoteWindows("C:\\dir\\"), "\"C:\\dir\\\\\"");      // a trailing backslash would eat the quote
  EXPECT_EQ(shell::quoteWindows("a\\\"b"), "\"a\\\\\"\"b\"");           // backslash before a quote is doubled
  EXPECT_EQ(shell::quoteWindows(""), "\"\"");
}

TEST(Shell, LevelsReadWholeWords) {
  using L = LogBook::Level;
  EXPECT_EQ(shell::levelOf("ERROR TS2304: Cannot find name 'nope'."), L::Error);
  EXPECT_EQ(shell::levelOf("FAILURE 1 compile error(s)"), L::Error);
  EXPECT_EQ(shell::levelOf("asc failed for assets/scripts/bad.ts: exit status 1"), L::Error);
  EXPECT_EQ(shell::levelOf("[warning] atlas region missing"), L::Warning);
  EXPECT_EQ(shell::levelOf("Copied asset: assets/ui/error_screen.ui.html"), L::Info);
  EXPECT_EQ(shell::levelOf("Copied asset: assets/error.png"), L::Info);
  EXPECT_EQ(shell::levelOf("Copied asset: assets/failed/x.png"), L::Info);
  EXPECT_EQ(shell::levelOf("Build failed."), L::Error);
  EXPECT_EQ(shell::levelOf("Built script: assets/scripts/errorHandler.ts"), L::Info);
  EXPECT_EQ(shell::levelOf("Checked 12 files, 0 errors"), L::Info);
  EXPECT_EQ(shell::levelOf("Build complete!"), L::Info);
}

#ifndef _WIN32
TEST(Shell, ProcessStreamsOutputAndExitStatus) {
  shell::Process process;
  ASSERT_TRUE(process.start("echo one; echo two 1>&2; exit 3"));
  std::vector<std::string> lines;
  for (std::string line; process.readLine(line);) lines.push_back(line);
  EXPECT_EQ(lines, (std::vector<std::string>{"one", "two"}));
  EXPECT_EQ(process.wait(), 3);
  EXPECT_FALSE(process.cancelled());
}

// A hung job (here a shell waiting on its own child) stops at once, child and all.
TEST(Shell, CancelStopsTheCommandAndWhatItStarted) {
  shell::Process process;
  ASSERT_TRUE(process.start("sleep 30 & echo started; wait"));
  std::string line;
  ASSERT_TRUE(process.readLine(line));
  EXPECT_EQ(line, "started");
  const auto begin = std::chrono::steady_clock::now();
  process.cancel();
  while (process.readLine(line)) {
  }  // ends only once every holder of the output (the sleep too) is gone
  EXPECT_EQ(process.wait(), -1);
  EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::seconds(5));
}

TEST(Shell, DestroyingARunningProcessStopsIt) {
  const auto begin = std::chrono::steady_clock::now();
  {
    shell::Process process;
    ASSERT_TRUE(process.start("sleep 30"));
  }
  EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::seconds(5));
}
#endif
