#include <gtest/gtest.h>

#include <fstream>

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

// A chosen app removed mid-session is noticed, so the editor can look again.
TEST(CodeEditors, PresentOnlyWhileItsProgramIsThere) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "jm-code-editors-test";
  fs::remove_all(dir);
  fs::create_directories(dir / "Zed.app");
  std::ofstream(dir / "code") << "#!/bin/sh\n";
  const CodeEditor tool{"vscode", "Visual Studio Code", dir / "code"};
  const CodeEditor bundle{"zed", "Zed", dir / "Zed.app"};
  EXPECT_TRUE(tool.present());
  EXPECT_TRUE(bundle.present());
  fs::remove_all(dir);
  EXPECT_FALSE(tool.present());
  EXPECT_FALSE(bundle.present());
  const CodeEditor gedit{"text", "Text Editor", dir / "gedit"};  // Linux: a found path, gone too
  EXPECT_FALSE(gedit.present());
  EXPECT_TRUE((CodeEditor{"text", "Text Editor", ""}.present()));
  EXPECT_TRUE((CodeEditor{"text", "Notepad", "notepad"}.present()));
}
