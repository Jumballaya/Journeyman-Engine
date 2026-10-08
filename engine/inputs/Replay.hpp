#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

#include "InputsManager.hpp"

namespace inputs {

// A replayed key press or release (JM_INPUT_REPLAY): on frame `frame`.
struct ReplayEvent {
  uint64_t frame;
  bool down;
  Key key;
};

// A replay file: one event per line, "<frame> down|up <KeyName>" ('#' starts a
// comment), in frame order (events on one frame keep the file's order). Lines
// that aren't events are skipped, and handed to `skipped` when given.
std::vector<ReplayEvent> parseReplay(std::istream& in, std::vector<std::string>* skipped = nullptr);

}  // namespace inputs
