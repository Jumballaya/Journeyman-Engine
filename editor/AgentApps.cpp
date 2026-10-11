#include "AgentApps.hpp"

#include <filesystem>

#include <nlohmann/json.hpp>

#include "CliRunner.hpp"
#include "Shell.hpp"

namespace {

AgentApps::Status statusOf(const std::string& s) {
  if (s == "connected") return AgentApps::Status::Connected;
  if (s == "not-connected") return AgentApps::Status::NotConnected;
  if (s == "manual") return AgentApps::Status::Manual;
  return AgentApps::Status::NotInstalled;
}

}  // namespace

AgentApps::~AgentApps() {
  {
    std::lock_guard lock(_mutex);
    _stopping = true;
    if (_process) _process->cancel();
  }
  if (_thread.joinable()) _thread.join();
}

std::optional<std::vector<AgentApps::App>> AgentApps::parse(const std::string& doctorJson) {
  const size_t start = doctorJson.find('{');  // past anything printed before it
  if (start == std::string::npos) return std::nullopt;
  const auto report = nlohmann::json::parse(doctorJson.begin() + static_cast<long>(start), doctorJson.end(), nullptr, false);
  if (!report.is_object() || !report.contains("agents")) return std::nullopt;
  std::vector<App> apps;
  for (const auto& a : report["agents"]) {
    if (!a.is_object()) continue;
    App app{a.value("name", ""), a.value("title", ""), statusOf(a.value("status", ""))};
    if (!app.name.empty()) apps.push_back(std::move(app));
  }
  return apps;
}

std::optional<std::vector<AgentApps::App>> AgentApps::apps() {
  if (!_asked) refresh();
  std::lock_guard lock(_mutex);
  auto apps = _apps;
  if (apps) {
    for (App& app : *apps) app.needsRestart = _restart.count(app.name) > 0;
  }
  return apps;
}

void AgentApps::setFolder(const std::filesystem::path& folder) {
  std::lock_guard lock(_mutex);
  if (folder == _folder) return;
  _folder = folder;
  _asked = false;
}

void AgentApps::refresh() { run({}); }

void AgentApps::connect(const std::string& name, bool on) {
  std::vector<std::string> setup = {"setup", name};
  if (!on) setup.insert(setup.begin() + 1, "--remove");
  run(std::move(setup));
}

std::optional<AgentApps::Change> AgentApps::lastChange() const {
  std::lock_guard lock(_mutex);
  return _change;
}

std::string AgentApps::error() const {
  std::lock_guard lock(_mutex);
  return _error;
}

void AgentApps::run(std::vector<std::string> setup) {
  if (_busy.exchange(true)) return;
  _asked = true;
  if (_thread.joinable()) _thread.join();
  _thread = std::thread([this, setup = std::move(setup)]() {
    // Starts jm with `args` in the folder; false if it couldn't (no jm, or closing).
    auto start = [this](const std::vector<std::string>& args) {
      std::lock_guard lock(_mutex);
      std::error_code ec;
      const std::string command = CliRunner::commandLine(_folder.empty() ? std::filesystem::current_path(ec) : _folder, args);
      if (_stopping || command.empty()) return false;
      _process = std::make_unique<shell::Process>();
      return _process->start(command);
    };
    // Runs jm with `args`: its output, and whether it exited 0.
    auto jm = [&](const std::vector<std::string>& args, std::string& output) {
      if (!start(args)) return false;
      for (std::string line; _process->readLine(line);) output += line + "\n";
      return _process->wait() == 0;
    };
    if (!setup.empty()) {
      std::string output;
      Change change{{}, jm(setup, output)};
      for (size_t at = 0, end; at < output.size(); at = end + 1) {
        end = output.find('\n', at);
        if (end > at) change.lines.push_back(output.substr(at, end - at));
      }
      std::lock_guard lock(_mutex);
      _change = std::move(change);
      if (_change->ok && setup.back() != "chatgpt") _restart.insert(setup.back());
    }
    std::string output;
    jm({"doctor", "--json"}, output);  // exits 1 on any problem, still with its report
    auto apps = parse(output);
    std::lock_guard lock(_mutex);
    if (apps) {
      _apps = std::move(apps);
      _error.clear();
    } else {
      _error = CliRunner::locate().empty() ? "Can't find the jm CLI: put it beside the editor, on PATH, or set JM_CLI."
                                           : "jm doctor didn't answer: " + output.substr(0, 300);
    }
    _busy = false;
  });
}
