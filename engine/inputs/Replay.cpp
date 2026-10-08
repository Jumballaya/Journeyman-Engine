#include "Replay.hpp"

#include <algorithm>
#include <sstream>
#include <variant>

#include "InputActions.hpp"

namespace inputs {

std::vector<ReplayEvent> parseReplay(std::istream& in, std::vector<std::string>* skipped) {
  std::vector<ReplayEvent> events;
  for (std::string line; std::getline(in, line);) {
    if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    std::istringstream fields(line);
    uint64_t frame;
    std::string action, keyName;
    if (!(fields >> frame >> action >> keyName)) {
      if (skipped && line.find_first_not_of(" \t\r") != std::string::npos) skipped->push_back(line);
      continue;
    }
    auto control = parseControl(keyName);
    if (!control || !std::holds_alternative<Key>(*control) || (action != "down" && action != "up")) {
      if (skipped) skipped->push_back(line);
      continue;
    }
    events.push_back({frame, action == "down", std::get<Key>(*control)});
  }
  std::stable_sort(events.begin(), events.end(), [](const ReplayEvent& a, const ReplayEvent& b) { return a.frame < b.frame; });
  return events;
}

}  // namespace inputs
