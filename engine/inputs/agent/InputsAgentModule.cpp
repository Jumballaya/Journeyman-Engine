#include <filesystem>
#include <fstream>
#include <variant>
#include <vector>

#include "../../core/app/Engine.hpp"
#include "../../core/app/ModuleTags.hpp"
#include "../../core/app/ModuleTraits.hpp"
#include "../../core/app/Registration.hpp"
#include "../../core/logger/logging.hpp"
#include "../InputsModule.hpp"
#include "Replay.hpp"

// The agent tools' keys (the agent build links this, the player doesn't):
// a replay file's (JM_INPUT_REPLAY), which then replace the devices entirely,
// and the driver's "down|up|press <Key>" (JM_DRIVE), written to
// JM_DRIVE_RECORD as a replay file. Each pressed in the inputs' step, where a
// device's key is.
class InputsAgentModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  bool driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) override;
  const char* name() const override { return "InputsAgentModule"; }

 private:
  InputsModule* _inputs = nullptr;
  std::vector<inputs::ReplayEvent> _replay;  // the file's, then the driver's presses' releases
  size_t _cursor = 0;
  uint64_t _frame = 0;    // the inputs' steps so far
  std::ofstream _record;  // JM_DRIVE_RECORD

  void loadReplay(const std::filesystem::path& path);
};

template <>
struct ModuleTraits<InputsAgentModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<InputsTag>;
};

REGISTER_MODULE(InputsAgentModule)

void InputsAgentModule::initialize(Engine& app) {
  _inputs = app.getModules().find<InputsModule>();
  if (!_inputs) return;
  const DevOptions& dev = app.getDevOptions();
  if (!dev.inputReplay.empty()) loadReplay(dev.inputReplay);
  if (dev.drive && !dev.driveRecord.empty()) {
    _record.open(dev.driveRecord, std::ios::trunc);
    if (_record) _record << "# Inputs of a driven run (JM_DRIVE); play it back with JM_INPUT_REPLAY.\n";
    else JM_LOG_ERROR("[Inputs] JM_DRIVE_RECORD: cannot write '{}'", dev.driveRecord.string());
  }
  _inputs->onKeysDue([this, &app]() {
    while (_cursor < _replay.size() && _replay[_cursor].frame <= _frame) {
      const auto& e = _replay[_cursor++];
      _inputs->setKey(app, e.key, e.down);
    }
    ++_frame;
  });
}

bool InputsAgentModule::driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) {
  if (!_inputs || (verb != "down" && verb != "up" && verb != "press")) return false;
  const auto control = inputs::parseControl(args);
  if (!control || !std::holds_alternative<inputs::Key>(*control)) {
    reply = {{"ok", false}, {"error", "unknown key '" + std::string(args) + "' (names as in replay files: Space, Enter, A, ArrowLeft...)"}};
    return true;
  }
  const inputs::Key key = std::get<inputs::Key>(*control);
  // Applied now, the next frame's systems see it: in a replay that's an event
  // on the frame before (frame 0 at the start, the earliest a replay can say).
  const uint64_t replayFrame = _frame > 0 ? _frame - 1 : 0;
  auto record = [&](uint64_t frame, bool down) {
    if (_record) _record << frame << (down ? " down " : " up ") << args << std::endl;
  };
  _inputs->setKey(app, key, verb != "up");
  record(replayFrame, verb != "up");
  if (verb == "press") {  // released after the next frame
    _replay.push_back({_frame, false, key});
    record(_frame, false);
  }
  reply = {{"ok", true}};
  return true;
}

void InputsAgentModule::loadReplay(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    JM_LOG_ERROR("[Inputs] JM_INPUT_REPLAY: cannot open '{}'", path.string());
    return;
  }
  std::vector<std::string> skipped;
  _replay = inputs::parseReplay(in, &skipped);
  for (const std::string& line : skipped) JM_LOG_WARN("[Inputs] replay: skipping '{}'", line);
  JM_LOG_INFO("[Inputs] replaying {} input events from {}", _replay.size(), path.string());
}
