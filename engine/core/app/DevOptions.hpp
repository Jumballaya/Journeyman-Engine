#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Development / automation switches, read once from JM_* environment
// variables (documented in docs/testing.md). Empty/zero means "off".
struct DevOptions {
  bool headless = false;                     // JM_HEADLESS: hidden window
  float fixedDt = 0.0f;                      // JM_FIXED_DT: seconds per frame
  uint64_t exitAfterFrames = 0;              // JM_EXIT_AFTER_FRAMES
  std::string entryScene;                    // JM_ENTRY_SCENE
  std::filesystem::path saveDir;             // JM_SAVE_DIR
  std::filesystem::path captureDir;          // JM_CAPTURE_DIR
  std::vector<uint64_t> captureFrames;       // JM_CAPTURE_FRAMES=60,120
  std::filesystem::path inputReplay;         // JM_INPUT_REPLAY

  static DevOptions fromEnvironment();
};
