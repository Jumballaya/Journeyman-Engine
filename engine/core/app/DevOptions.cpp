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
  o.errorsOut = env("JM_ERRORS");
  const std::string strict = env("JM_STRICT");
  o.strict = !strict.empty() && strict != "0";
  if (auto v = env("JM_SEED"); !v.empty()) o.seed = std::strtoull(v.c_str(), nullptr, 10);
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
