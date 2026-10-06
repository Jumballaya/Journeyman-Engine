#include "DevOptions.hpp"

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
  if (auto v = env("JM_FIXED_DT"); !v.empty()) o.fixedDt = std::strtof(v.c_str(), nullptr);
  if (auto v = env("JM_EXIT_AFTER_FRAMES"); !v.empty()) o.exitAfterFrames = std::strtoull(v.c_str(), nullptr, 10);
  o.entryScene = env("JM_ENTRY_SCENE");
  o.saveDir = env("JM_SAVE_DIR");
  o.captureDir = env("JM_CAPTURE_DIR");
  o.inputReplay = env("JM_INPUT_REPLAY");
  std::stringstream frames(env("JM_CAPTURE_FRAMES"));
  for (std::string item; std::getline(frames, item, ',');) {
    if (!item.empty()) o.captureFrames.push_back(std::strtoull(item.c_str(), nullptr, 10));
  }
  return o;
}
