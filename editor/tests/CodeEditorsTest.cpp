#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <optional>

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
  std::filesystem::permissions(dir / "code", std::filesystem::perms::owner_all);
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

#ifndef _WIN32
namespace {

namespace fs = std::filesystem;

void writeTool(const fs::path& p, const std::string& body = "#!/bin/sh\n") {
  std::ofstream(p) << body;
  fs::permissions(p, fs::perms::owner_all);
}

// Sets an environment variable for the test's scope.
struct ScopedEnv {
  std::string name;
  std::optional<std::string> old;
  ScopedEnv(std::string n, const std::string& value) : name(std::move(n)) {
    if (const char* v = std::getenv(name.c_str())) old = v;
    setenv(name.c_str(), value.c_str(), 1);
  }
  ~ScopedEnv() { old ? setenv(name.c_str(), old->c_str(), 1) : unsetenv(name.c_str()); }
};

const CodeEditor* withId(const std::vector<CodeEditor>& apps, const std::string& id) {
  for (const CodeEditor& app : apps) {
    if (app.id == id) return &app;
  }
  return nullptr;
}

}  // namespace

// A tool that can't run is passed over for a later one on PATH, and stops counting once it can't.
TEST(CodeEditors, SkipsANonexecutableToolForALaterOne) {
  if (fs::exists("/Applications/Sublime Text.app")) GTEST_SKIP() << "Sublime Text is installed here";
  const fs::path dir = fs::temp_directory_path() / "jm-code-editors-path";
  fs::remove_all(dir);
  fs::create_directories(dir / "a");
  fs::create_directories(dir / "b");
  std::ofstream(dir / "a" / "subl") << "#!/bin/sh\n";  // not executable
  writeTool(dir / "b" / "subl");
  writeTool(dir / "a" / "mdfind");  // macOS: Spotlight knows nothing
  const ScopedEnv path("PATH", (dir / "a").string() + ":" + (dir / "b").string());
  const ScopedEnv home("HOME", (dir / "home").string());
  const auto found = findCodeEditors();
  const CodeEditor* subl = withId(found, "sublime");
  ASSERT_NE(subl, nullptr);
  EXPECT_EQ(subl->program, dir / "b" / "subl");
  EXPECT_TRUE(subl->present());
  fs::permissions(dir / "b" / "subl", fs::perms::owner_read | fs::perms::owner_write);
  EXPECT_FALSE(subl->present());
  fs::remove_all(dir);
}
#endif

#ifdef __APPLE__
// Spotlight can still list a bundle that was deleted: the first that exists wins.
TEST(CodeEditors, SkipsStaleSpotlightResults) {
  if (fs::exists("/Applications/Sublime Text.app")) GTEST_SKIP() << "Sublime Text is installed here";
  const fs::path dir = fs::temp_directory_path() / "jm-code-editors-spotlight";
  fs::remove_all(dir);
  fs::create_directories(dir / "bin");
  const fs::path real = dir / "Elsewhere" / "Sublime Text.app", gone = dir / "Gone" / "Sublime Text.app";
  fs::create_directories(real);
  const ScopedEnv path("PATH", (dir / "bin").string());
  const ScopedEnv home("HOME", (dir / "home").string());
  writeTool(dir / "bin" / "mdfind", "#!/bin/sh\necho '" + gone.string() + "'\necho '" + real.string() + "'\n");
  const auto found = findCodeEditors();
  const CodeEditor* subl = withId(found, "sublime");
  ASSERT_NE(subl, nullptr);
  EXPECT_EQ(subl->program, real);
  writeTool(dir / "bin" / "mdfind", "#!/bin/sh\necho '" + gone.string() + "'\n");
  EXPECT_EQ(withId(findCodeEditors(), "sublime"), nullptr);
  fs::remove_all(dir);
}
#endif
