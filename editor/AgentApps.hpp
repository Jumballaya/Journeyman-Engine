#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "Shell.hpp"

// The agent apps jm can connect to its MCP server, as `jm doctor --json` sees
// them, and connecting or disconnecting one (`jm setup [--remove] <app>`).
// jm runs in the background, one job at a time.
class AgentApps {
 public:
  enum class Status { Connected, NotConnected, NotInstalled, Manual };  // Manual: ChatGPT, set up by hand
  struct App {
    std::string name;   // what jm setup takes: "claude-code"
    std::string title;  // "Claude Code"
    Status status = Status::NotInstalled;
    bool needsRestart = false;  // changed by this editor: the app's open sessions don't know yet
  };
  // What the last jm setup said: a line each (which file, which entry).
  struct Change {
    std::vector<std::string> lines;
    bool ok = false;
  };

  ~AgentApps();  // stops jm, if it's running

  // The apps, or nullopt until jm first answers (the first call asks it).
  std::optional<std::vector<App>> apps();
  // Where jm runs: the open game's folder, as Claude Code's project scopes are
  // per folder. A new one makes the next apps() ask again.
  void setFolder(const std::filesystem::path& folder);
  // Asks jm again; does nothing while it's busy.
  void refresh();
  // jm setup [--remove] <name>, then a refresh. Does nothing while busy.
  void connect(const std::string& name, bool on);
  bool busy() const { return _busy; }
  std::optional<Change> lastChange() const;
  // Why the last ask got no answer: no jm, or one that didn't say. Empty otherwise.
  std::string error() const;

  // `jm doctor --json`'s agents; nullopt if it isn't doctor's JSON.
  static std::optional<std::vector<App>> parse(const std::string& doctorJson);

 private:
  void run(std::vector<std::string> setup);  // setup args (none: just ask), then doctor

  std::thread _thread;
  std::unique_ptr<shell::Process> _process;  // the one running now
  std::atomic<bool> _busy{false};
  mutable std::mutex _mutex;
  std::optional<std::vector<App>> _apps;
  std::optional<Change> _change;
  std::set<std::string> _restart;  // apps this editor changed
  std::string _error;
  std::filesystem::path _folder;
  bool _asked = false;
  bool _stopping = false;  // the editor is closing: start no more jm
};
