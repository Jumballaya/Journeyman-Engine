#include "DevOptions.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {
std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}
}  // namespace

DevOptions DevOptions::fromEnvironment() {
  DevOptions o;
  const std::string headless = env("JM_HEADLESS");
  o.headless = !headless.empty() && headless != "0";
  o.renderer = env("JM_RENDERER");
  const std::string drive = env("JM_DRIVE");
  o.drive = !drive.empty() && drive != "0";
  o.driveRecord = env("JM_DRIVE_RECORD");
  if (o.renderer == "none") o.headless = true;
  if (auto v = env("JM_FIXED_DT"); !v.empty()) o.fixedDt = std::strtof(v.c_str(), nullptr);
  if (auto v = env("JM_EXIT_AFTER_FRAMES"); !v.empty()) o.exitAfterFrames = std::strtoull(v.c_str(), nullptr, 10);
  o.entryScene = env("JM_ENTRY_SCENE");
  o.sessionFile = env("JM_SESSION");
  o.saveDir = env("JM_SAVE_DIR");
  o.captureDir = env("JM_CAPTURE_DIR");
  o.inputReplay = env("JM_INPUT_REPLAY");
  o.errorsOut = env("JM_ERRORS");
  const std::string strict = env("JM_STRICT");
  o.strict = !strict.empty() && strict != "0";
  if (auto v = env("JM_WINDOW_POS"); !v.empty()) {
    int x = 0, y = 0;
    if (std::sscanf(v.c_str(), "%d,%d", &x, &y) == 2) o.windowPos = std::make_pair(x, y);
  }
  const std::string realtime = env("JM_REALTIME");
  o.realtime = !realtime.empty() && realtime != "0";
  if (auto v = env("JM_SEED"); !v.empty()) o.seed = std::strtoull(v.c_str(), nullptr, 10);
  o.recordDir = env("JM_RECORD_DIR");
  o.playSession = env("JM_PLAY_SESSION");
  o.afterReplay = o.drive                         ? AfterReplay::Drive
                  : env("JM_PLAY_THEN") == "live" ? AfterReplay::Live
                                                  : AfterReplay::Stop;
  if (auto v = env("JM_PLAY_UNTIL"); !v.empty()) o.playUntil = std::strtoull(v.c_str(), nullptr, 10);
  if (o.automated()) {
    if (!(o.fixedDt > 0.0f)) o.fixedDt = 1.0f / 60.0f;
    if (!o.seed) o.seed = 1;
  }
  auto frameList = [](const std::string& text) {
    std::vector<uint64_t> out;
    std::stringstream items(text);
    for (std::string item; std::getline(items, item, ',');) {
      if (!item.empty()) out.push_back(std::strtoull(item.c_str(), nullptr, 10));
    }
    return out;
  };
  o.captureFrames = frameList(env("JM_CAPTURE_FRAMES"));
  o.dumpDir = env("JM_DUMP_DIR");
  o.dumpFrames = frameList(env("JM_DUMP_FRAMES"));
  return o;
}
