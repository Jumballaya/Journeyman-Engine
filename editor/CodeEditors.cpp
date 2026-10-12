#include "CodeEditors.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "Shell.hpp"

namespace fs = std::filesystem;

namespace {

struct Known {
  const char* id;
  const char* name;
  const char* bundleId;  // macOS
  const char* app;       // macOS: the bundle's name in Applications
  const char* appTool;   // macOS: its command-line tool, inside the bundle
  std::vector<const char*> windows;  // Windows: where installers put the tool, under an environment variable
  std::vector<const char*> onPath;   // its tool's names on PATH
};

const std::vector<Known>& known() {
  static const std::vector<Known> apps = {
      {"vscode", "Visual Studio Code", "com.microsoft.VSCode", "Visual Studio Code.app", "Contents/Resources/app/bin/code",
       {"LOCALAPPDATA/Programs/Microsoft VS Code/bin/code.cmd", "ProgramFiles/Microsoft VS Code/bin/code.cmd"}, {"code"}},
      {"cursor", "Cursor", "com.todesktop.230313mzl4w4u92", "Cursor.app", "Contents/Resources/app/bin/cursor",
       {"LOCALAPPDATA/Programs/cursor/resources/app/bin/cursor.cmd"}, {"cursor"}},
      {"zed", "Zed", "dev.zed.Zed", "Zed.app", "Contents/MacOS/cli", {"LOCALAPPDATA/Programs/Zed/bin/zed.exe"}, {"zed", "zeditor"}},
      {"sublime", "Sublime Text", "com.sublimetext.4", "Sublime Text.app", "Contents/SharedSupport/bin/subl",
       {"ProgramFiles/Sublime Text/subl.exe"}, {"subl"}},
  };
  return apps;
}

bool isFile(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

fs::path onPath(const std::string& name) {
#ifdef _WIN32
  const char sep = ';';
  const std::vector<std::string> suffixes = {".exe", ".cmd"};
#else
  const char sep = ':';
  const std::vector<std::string> suffixes = {""};
#endif
  std::istringstream dirs(std::getenv("PATH") ? std::getenv("PATH") : "");
  for (std::string dir; std::getline(dirs, dir, sep);) {
    if (dir.size() >= 4 && dir.compare(dir.size() - 4, 4, "sbin") == 0) continue;  // daemons: /usr/sbin/zed is ZFS's
    for (const std::string& suffix : suffixes) {
      if (!dir.empty() && isFile(fs::path(dir) / (name + suffix))) return fs::path(dir) / (name + suffix);
    }
  }
  return {};
}

#ifdef __APPLE__
// The app's bundle: in an Applications folder, else wherever Spotlight knows it.
fs::path findApp(const Known& k) {
  const char* home = std::getenv("HOME");
  for (const fs::path& dir : {fs::path("/Applications"), fs::path(home ? home : "") / "Applications"}) {
    std::error_code ec;
    if (fs::is_directory(dir / k.app, ec)) return dir / k.app;
  }
  shell::Process mdfind;
  if (!mdfind.start(std::string("mdfind ") + shell::quote(std::string("kMDItemCFBundleIdentifier == '") + k.bundleId + "'") +
                    " 2>/dev/null")) {
    return {};
  }
  std::string first;
  for (std::string line; mdfind.readLine(line);) {
    if (first.empty() && line.size() > 4 && line.compare(line.size() - 4, 4, ".app") == 0) first = line;
  }
  mdfind.wait();
  return first;
}
#endif

// Where `k` is installed here: its tool (or macOS bundle), or empty.
fs::path findProgram(const Known& k) {
#ifdef __APPLE__
  if (const fs::path app = findApp(k); !app.empty()) return isFile(app / k.appTool) ? app / k.appTool : app;
#elif defined(_WIN32)
  for (const std::string place : k.windows) {
    const size_t slash = place.find('/');
    const char* base = std::getenv(place.substr(0, slash).c_str());
    if (base && isFile(fs::path(base) / fs::path(place.substr(slash + 1)))) return fs::path(base) / fs::path(place.substr(slash + 1));
  }
#endif
  for (const char* name : k.onPath) {
    if (const fs::path p = onPath(name); !p.empty()) return p;
  }
  return {};
}

CodeEditor plainTextEditor() {
#ifdef __APPLE__
  return {"text", "TextEdit", "open"};
#elif defined(_WIN32)
  return {"text", "Notepad", "notepad"};
#else
  for (const char* name : {"gnome-text-editor", "gedit", "kate", "mousepad", "xed"}) {
    if (const fs::path p = onPath(name); !p.empty()) return {"text", "Text Editor", p};
  }
  return {"text", "Text Editor", ""};  // the desktop's default for text/plain
#endif
}

}  // namespace

std::string CodeEditor::command(const fs::path& root, const fs::path& file, int line) const {
  const std::string at = file.string() + (line > 0 ? ":" + std::to_string(line) : "");
  std::string command;
  if (id == "text") {
#ifdef __APPLE__
    command = "open -t " + shell::quote(file.string());
#else
    command = program.empty() ? "gtk-launch \"$(xdg-mime query default text/plain)\" " + shell::quote(file.string())
                              : shell::quote(program.string()) + " " + shell::quote(file.string());
#endif
  } else if (program.extension() == ".app") {
    command = "open -a " + shell::quote(program.string()) + " " + shell::quote(file.string());
  } else {
    command = shell::quote(program.string()) + " " + shell::quote(root.string()) + " ";
    command += (id == "vscode" || id == "cursor" ? "-g " : "") + shell::quote(at);  // Zed, Sublime: file:line
  }
#ifdef _WIN32
  return "\"" + command + "\"";  // cmd /c strips the outer quotes of a line that starts with one
#else
  return command;
#endif
}

bool CodeEditor::present() const {
  std::error_code ec;
  return !program.has_parent_path() || fs::exists(program, ec);  // a bare name is a system command
}

std::vector<CodeEditor> findCodeEditors() {
  std::vector<CodeEditor> found;
  for (const Known& k : known()) {
    if (const fs::path program = findProgram(k); !program.empty()) found.push_back({k.id, k.name, program});
  }
  found.push_back(plainTextEditor());
  return found;
}
