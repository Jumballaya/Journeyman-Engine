#pragma once

#include <filesystem>
#include <string>
#include <vector>

// An app that edits scripts. A script is never opened "by type": macOS takes
// .ts for an MPEG-2 video, so it goes to an app found here by name.
struct CodeEditor {
  std::string id;    // what the editor remembers: "vscode", "text" (the system's plain-text editor)
  std::string name;  // "Visual Studio Code"
  std::filesystem::path program;  // its command-line tool, or (macOS, none in the bundle) the .app itself

  // The shell command opening `file` (at `line`, when the app can) in the
  // project folder `root`, so the app sees the project's TypeScript setup.
  std::string command(const std::filesystem::path& root, const std::filesystem::path& file, int line) const;
  // Whether its program is still where it was found.
  bool present() const;
};

// The code editors installed here, the best known first; the system's
// plain-text editor ("text") is always last.
std::vector<CodeEditor> findCodeEditors();
