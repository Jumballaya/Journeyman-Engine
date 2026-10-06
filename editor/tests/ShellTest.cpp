#include <gtest/gtest.h>

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
  EXPECT_EQ(shell::levelOf("Built script: assets/scripts/errorHandler.ts"), L::Info);
  EXPECT_EQ(shell::levelOf("Checked 12 files, 0 errors"), L::Info);
  EXPECT_EQ(shell::levelOf("Build complete!"), L::Info);
}
