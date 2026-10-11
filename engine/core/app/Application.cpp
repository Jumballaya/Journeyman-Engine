#include "Application.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

#include "../assets/Archive.hpp"
#include "../ecs/component/SchemaJson.hpp"
#include "../logger/logging.hpp"
#include "Engine.hpp"
#include "ErrorReport.hpp"
#include "Platform.hpp"

namespace {

// An exported game: game.jm in its .app's Resources (macOS, so the signed
// executable stays plain), else inside the executable or beside it. Empty if none.
std::filesystem::path findBundledArchive() {
  if (const auto exe = platform::executablePath(); !exe.empty() && Archive::isEmbeddedIn(exe)) return exe;
  const auto exeDir = platform::executableDir();
  if (exeDir.empty()) return {};
  for (const auto& candidate : {exeDir / "game.jm", exeDir / ".." / "Resources" / "game.jm"}) {
    if (std::filesystem::is_regular_file(candidate)) return candidate.lexically_normal();
  }
  return {};
}

// Dev runs log to ./logs; a standalone game logs into its per-user data dir,
// named after the executable (jm export names it after the game).
std::unique_ptr<Logger> makeLogger(bool standalone) {
  std::vector<std::filesystem::path> candidates;
  if (!standalone) candidates.push_back("logs/engine.log");
  candidates.push_back(platform::userDataDir(platform::executablePath().stem().string()) / "logs" / "engine.log");
  candidates.push_back(std::filesystem::temp_directory_path() / "journeyman" / "engine.log");
  for (const auto& path : candidates) {
    try {
      return std::make_unique<Logger>("engine", path.string());
    } catch (const std::exception&) {
      spdlog::drop("engine");
    }
  }
  return nullptr;
}

// Ctrl-C or a service manager's stop (SIGTERM): finish the frame and shut
// down properly (a server's players are told, logs and saves are written).
std::atomic<Engine*> interruptible{nullptr};
void onStopSignal(int) {
  if (Engine* engine = interruptible.load()) engine->quit();
}

}  // namespace

int Application::run() {
  // Every component's scene JSON and script fields, as JSON on stdout: the
  // schema tools read. Needs no project, window or GL.
  if (_argc > 1 && std::string_view(_argv[1]) == "--schema") {
    Engine engine(".", std::string(kManifestEntryKey), EngineOptions{.server = _server});
    engine.declare();
    std::cout << schemaJson(engine.getWorld().getComponentRegistry(), engine.getScriptManager().signatures()).dump(2) << "\n";
    return 0;
  }

  const std::filesystem::path bundled = _argc > 1 ? std::filesystem::path{} : findBundledArchive();
  auto logger = makeLogger(!bundled.empty());
  if (!logger) {
    std::cerr << "Journeyman: could not open any log file\n";
    return 1;
  }
  LoggerService::initialize(std::move(logger));
  JM_LOG_INFO("Journeyman Engine Starting up...");

  // Errors for tools (JM_ERRORS), and JM_STRICT: the first one ends the run.
  const DevOptions dev = DevOptions::fromEnvironment();
  ErrorReport errors(dev.errorsOut);
  Engine* engineRunning = nullptr;
  LoggerService::instance().setErrorListener([&](LogLevel level, std::string_view message, const ErrorSource& source) {
    const uint64_t frame = engineRunning ? engineRunning->frameCount() : 0;
    errors.add(level, message, source, frame);
    if (dev.drive && engineRunning) engineRunning->noteError(ErrorReport::toJson(level, message, source, frame));
    if (dev.strict && engineRunning) engineRunning->quit();
  });
  struct StopListening {
    ~StopListening() { LoggerService::instance().setErrorListener(nullptr); }
  } stopListening;

  std::filesystem::path input = std::string(kManifestEntryKey);
  if (_argc > 1) {
    input = _argv[1];
  } else if (!bundled.empty()) {
    input = bundled;
  }

  // An archive (.jm, or a game executable) or a game folder holds its manifest
  // under kManifestEntryKey; a .json path is the manifest itself.
  std::filesystem::path rootDir = input;
  std::filesystem::path manifestPath = std::string(kManifestEntryKey);
  if (input.extension() == ".jm" || input == bundled) {
    if (!std::filesystem::is_regular_file(input)) {
      JM_LOG_ERROR("[Archive] not a regular file: {}", input.string());
      return 1;
    }
  } else if (input.extension() == ".json") {
    rootDir = input.parent_path();
    manifestPath = input;
  } else if (!std::filesystem::is_directory(input)) {
    JM_LOG_ERROR("Unknown input type. Must be a .json, directory, or .jm archive.");
    return 1;
  }
  JM_LOG_INFO("Mounting '{}', manifest '{}'", rootDir.string(), manifestPath.string());

  // An escaped exception (startup, or mid-game) is reported, not an abort().
  try {
    EngineOptions options;
    options.server = _server;
    Engine engine(rootDir, manifestPath, std::move(options));
    engineRunning = &engine;
    struct Forget {  // destroyed before the engine, however the scope ends
      Engine*& engine;
      ~Forget() { engine = nullptr; }
    } forget{engineRunning};
    interruptible = &engine;
    struct StopHandling {
      StopHandling() {
        std::signal(SIGINT, onStopSignal);
        std::signal(SIGTERM, onStopSignal);
      }
      ~StopHandling() {
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
        interruptible = nullptr;
      }
    } stopHandling;
    engine.initialize();
    engine.run();
  } catch (const std::exception& e) {
    JM_LOG_CRITICAL("Fatal: {}", e.what());
    LoggerService::instance().flush();
    std::cerr << "Journeyman: " << e.what() << "\n";
    return 1;
  }
  JM_LOG_INFO("Journeyman Engine Shut Down");
  LoggerService::instance().flush();
  if (dev.strict && errors.count() > 0) {
    std::cerr << "Journeyman: stopped at the first error (JM_STRICT): " << errors.first() << "\n";
    return 1;
  }
  return 0;
}
