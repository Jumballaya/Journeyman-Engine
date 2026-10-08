#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Development / automation switches, read once from JM_* environment
// variables (documented in docs/testing.md). Empty/zero means "off".
//
// An automated run (headless, or replaying input) is deterministic unless told
// otherwise: it steps a fixed 1/60 s and uses seed 1, so the same inputs give
// the same frames every time.
struct DevOptions {
  bool headless = false;                     // JM_HEADLESS: hidden window
  float fixedDt = 0.0f;                      // JM_FIXED_DT: seconds per frame
  std::optional<uint64_t> seed;              // JM_SEED: every random number in the run follows from it
  uint64_t exitAfterFrames = 0;              // JM_EXIT_AFTER_FRAMES
  std::string entryScene;                    // JM_ENTRY_SCENE
  std::filesystem::path saveDir;             // JM_SAVE_DIR
  std::filesystem::path captureDir;          // JM_CAPTURE_DIR
  std::vector<uint64_t> captureFrames;       // JM_CAPTURE_FRAMES=60,120
  std::filesystem::path inputReplay;         // JM_INPUT_REPLAY
  std::filesystem::path dumpDir;             // JM_DUMP_DIR: the game's state as JSON, at exit
  std::vector<uint64_t> dumpFrames;          // JM_DUMP_FRAMES=60,120: and at these frames
  std::string errorsOut;                     // JM_ERRORS: "-" (stderr) or a file; errors as JSON lines
  bool strict = false;                       // JM_STRICT: the first error ends the run, exit code 1

  static DevOptions fromEnvironment();

  bool automated() const { return headless || !inputReplay.empty(); }
};
