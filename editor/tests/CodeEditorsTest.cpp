#include <gtest/gtest.h>

#include "CodeEditors.hpp"

#ifndef _WIN32
// Each app gets the project folder and the file in the form it takes a line in.
TEST(CodeEditors, OpenTheProjectAndJumpToTheLine) {
  const CodeEditor vscode{"vscode", "Visual Studio Code", "/A/Code.app/Contents/Resources/app/bin/code"};
  EXPECT_EQ(vscode.command("/g", "/g/assets/scripts/a b.ts", 12),
            "'/A/Code.app/Contents/Resources/app/bin/code' '/g' -g '/g/assets/scripts/a b.ts:12'");
  const CodeEditor zed{"zed", "Zed", "/Applications/Zed.app/Contents/MacOS/cli"};
  EXPECT_EQ(zed.command("/g", "/g/s.ts", 0), "'/Applications/Zed.app/Contents/MacOS/cli' '/g' '/g/s.ts'");
  const CodeEditor bundle{"sublime", "Sublime Text", "/Applications/Sublime Text.app"};  // no tool inside
  EXPECT_EQ(bundle.command("/g", "/g/s.ts", 3), "open -a '/Applications/Sublime Text.app' '/g/s.ts'");
}
#endif

TEST(CodeEditors, ThePlainTextEditorIsAlwaysThereAndLast) {
  const auto found = findCodeEditors();
  ASSERT_FALSE(found.empty());
  EXPECT_EQ(found.back().id, "text");
}
