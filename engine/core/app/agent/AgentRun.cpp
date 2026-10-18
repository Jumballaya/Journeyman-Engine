#include "AgentRun.hpp"

#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <utility>

#include "../../logger/logging.hpp"
#include "../Engine.hpp"
#include "../Platform.hpp"
#include "../WindowEvents.hpp"

std::unique_ptr<AgentTools> AgentTools::make(Engine& engine, const DevOptions& dev) {
  return std::make_unique<AgentRun>(engine, dev);
}

AgentRun::AgentRun(Engine& engine, DevOptions dev)
    : _engine(engine),
      _dev(std::move(dev)),
      _playback(_dev.playSession.empty() ? nullptr : std::make_unique<session::Playback>(_dev.playSession)) {}

AgentRun::~AgentRun() {
  if (_replaySaveDir.empty()) return;
  std::error_code ec;
  std::filesystem::remove_all(_replaySaveDir, ec);
}

std::optional<uint64_t> AgentRun::seed() const {
  if (!_playback) return std::nullopt;
  return _playback->meta().value("seed", uint64_t{1});
}

std::optional<std::string> AgentRun::entryScene() const {
  if (!_playback) return std::nullopt;
  return _playback->meta().value("entryScene", _engine.getManifest().entryScene);
}

std::optional<bool> AgentRun::fullscreen() const {
  if (!_playback) return _dev.fullscreen;
  const auto it = _playback->meta().find("fullscreen");
  if (it == _playback->meta().end() || !it->is_boolean()) return std::nullopt;
  return it->get<bool>();
}

std::filesystem::path AgentRun::saveDir(std::filesystem::path usual) {
  if (!_playback || !_dev.saveDir.empty()) return usual;
  // A replay starts from the save the player had, and never touches theirs.
  _replaySaveDir = std::filesystem::temp_directory_path() /
                   ("jm-replay-" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(_replaySaveDir);
  std::error_code ec;
  std::filesystem::copy_file(_dev.playSession / "save.json", _replaySaveDir / "save.json", ec);
  return _replaySaveDir;
}

void AgentRun::start() {
  trackOrigins();
  startSession();
  if (_dev.drive || !_dev.profileOut.empty()) _engine.setProfiling(true);
  startReplay();
  startRecording();
}

void AgentRun::startSession() {
  GameState& session = _engine.getSession();
  if (_playback) {  // the values the recorded run started with
    // A named copy: items() of the temporary value() returns would outlive it.
    const nlohmann::json values = _playback->meta().value("sessionValues", nlohmann::json::object());
    for (const auto& [key, value] : values.items()) session.setJson(key, value);
    return;
  }
  const auto& path = _dev.sessionFile;
  if (path.empty()) return;
  std::ifstream in(path);
  const nlohmann::json values = nlohmann::json::parse(in, nullptr, false);
  if (!in.is_open() || !values.is_object()) {
    JM_REPORT_ERROR((ErrorSource{path.generic_string()}), "[Engine] JM_SESSION: '{}' isn't a readable JSON object",
                    path.string());
    return;
  }
  for (const auto& [key, value] : values.items()) session.setJson(key, value);
  JM_LOG_INFO("[Engine] session: {} values from {}", values.size(), path.string());
}

void AgentRun::startReplay() {
  if (!_playback) return;
  // The recording's size, before the first frame: pointer positions are in its pixels.
  const nlohmann::json size = _playback->meta().value("framebuffer", nlohmann::json());
  const int w = size.is_array() && size.size() == 2 && size[0].is_number_integer() ? size[0].get<int>() : 0;
  const int h = w > 0 && size[1].is_number_integer() ? size[1].get<int>() : 0;
  if (w <= 0 || h <= 0) return;
  _engine.getEventBus().emit(EVT_WindowResize, events::WindowResized{w, h});
  _engine.getEventBus().dispatch();
}

void AgentRun::startRecording() {
  if (_dev.recordDir.empty()) return;
  const GameManifest& manifest = _engine.getManifest();
  std::string startingSave;
  if (std::ifstream in(_engine.getSave().file()); in) startingSave.assign(std::istreambuf_iterator<char>(in), {});
  const nlohmann::json config = manifest.config.is_object() ? manifest.config : nlohmann::json::object();
  const Engine::ViewSize framebuffer = _engine.framebufferSize();
  nlohmann::json meta = {{"game", manifest.name},
                         {"gameVersion", manifest.version},
                         {"seed", _engine.getSeeds().seed()},
                         {"entryScene", _engine.entrySceneName()},
                         {"sessionValues", _engine.getSession().values()},
                         {"window", config.value("window", nlohmann::json::object())},
                         {"framebuffer", {framebuffer.width, framebuffer.height}},
                         {"gamepad", false}};
  if (_playback) meta["replayOf"] = _dev.playSession.filename().string();
  if (const auto full = fullscreen()) meta["fullscreen"] = *full;  // a replay launches the same
  try {
    _recorder = std::make_unique<session::Recorder>(_dev.recordDir, std::move(meta), startingSave);
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[Session] not recording: {}", e.what());
    _notRecordingNotice = std::string("No marker: recording couldn't start: ") + e.what();
    return;
  }
  JM_LOG_INFO("[Session] recording to {} (F8 drops a marker)", _dev.recordDir.string());

  // The pointer, wheel and window, at the frame the game sees them (dispatch).
  // Keys are recorded by the inputs module, by name: scancodes differ
  // between machines, a key's name doesn't.
  EventBus& bus = _engine.getEventBus();
  bus.subscribe<events::MouseMove>(EVT_MouseMove, [this](const events::MouseMove& e) {
    recordInput({{"type", "move"}, {"x", e.x}, {"y", e.y}});
  });
  bus.subscribe<events::MouseButton>(EVT_MouseButton, [this](const events::MouseButton& e) {
    recordInput({{"type", "button"}, {"button", e.button}, {"down", e.down}});
  });
  bus.subscribe<events::MouseWheel>(EVT_MouseWheel, [this](const events::MouseWheel& e) {
    recordInput({{"type", "wheel"}, {"dx", e.dx}, {"dy", e.dy}});
  });
  bus.subscribe<events::WindowResized>(EVT_WindowResize, [this](const events::WindowResized& e) {
    recordInput({{"type", "resize"}, {"w", e.width}, {"h", e.height}});
  });
}

bool AgentRun::runStarts() {
  if (_dev.drive) {
    drive(std::cin, std::cout);
    return true;
  }
  if (!_dev.control.empty()) _live = std::make_unique<LiveControl>(_dev.control);
  return false;
}

float AgentRun::beforeFrame() {
  if (_live) _live->poll(_engine);
  if (std::exchange(_stepHeld, false) && _held) _engine.stepOnce();
  if (_held) return 0.0f;  // live control's pause, as F8's moment holds it
  return _live ? _live->timeScale() : 1.0f;
}

void AgentRun::runEnds() {
  _live.reset();  // its folder goes with it
  dumpState("state_exit.json");
  if (Profiler* profiler = _engine.profiler(); profiler && !_dev.profileOut.empty()) {
    std::string error;
    if (!platform::writeAtomically(_dev.profileOut, profiler->report().dump(2) + "\n", error))
      JM_LOG_ERROR("[Engine] JM_PROFILE: {}", error);
  }
}

void AgentRun::frameDue() {
  const auto now = std::chrono::steady_clock::now();
  if (!_dev.watch || now - _lastWatch < std::chrono::milliseconds(500)) return;
  _lastWatch = now;
  _engine.reloadAssets(false);
}

float AgentRun::stepDue(float dt) {
  const uint64_t frame = _engine.frameCount();
  if (!replayingAt(frame)) return dt;
  // What the driver gave between frames, before the frame starts as it was then.
  replay(_playback->eventsBefore(frame));
  return _playback->dt(frame);  // the recorded run's steps, whatever this one's
}

void AgentRun::stepBegun() {
  if (_recorder) _recorder->frameStarts(_engine.frameCount(), _engine.windowFocused());
}

void AgentRun::devicesDue() {
  if (const uint64_t frame = _engine.frameCount(); replayingAt(frame)) replay(_playback->eventsAt(frame));
}

void AgentRun::stepDone(float dt) {
  const uint64_t frame = _engine.frameCount();
  std::optional<nlohmann::json> core;  // made at most once, for whichever needs it
  const session::LazyState state = [&]() -> const nlohmann::json& {
    if (!core) core = _engine.stateJson(false);
    return *core;
  };
  if (_recorder) {
    if (auto thumb = _recorder->frameDone(frame, dt, state)) _engine.requestCapture({*thumb, session::kThumbWidth});
  }
  if (replayingAt(frame) && !_divergedAt) {
    if (const auto matches = _playback->matchesAt(frame, state); matches && *matches) {
      _matchedAt = frame;
    } else if (matches) {
      _divergedAt = frame;
      JM_LOG_WARN("[Session] this replay differs from its recording from frame {} on (by frame {} at the latest)",
                  _matchedAt ? *_matchedAt + 1 : 0, frame);
    }
  }
  if (_held) _cleanPending = true;  // `.`: the marker's picture is of this frame now
  // The run ends at its frame, whatever the drawing rate.
  if (_dev.drive) return;
  const uint64_t ran = frame + 1;
  const bool replayEnded = _playback && _dev.afterReplay == DevOptions::AfterReplay::Stop && !replayingAt(ran);
  if ((_dev.exitAfterFrames > 0 && ran >= _dev.exitAfterFrames) || replayEnded) _engine.quit();
}

void AgentRun::frameDrawn(uint64_t firstStep) {
  // The held frame, drawn without the moment's marks: the picture a saved moment gets.
  if (std::exchange(_cleanPending, false) && _held) {
    nlohmann::json reply;
    if (_recorder) _engine.getModules().driveCommand(_engine, "capture", heldImage().string(), reply);
    if (_dev.drive) _engine.frame(0.0f);  // and with them: a driven run draws only when told
  }
  // The frames as drawn (and captured), for JM_DUMP_FRAMES.
  for (const uint64_t f : _dev.dumpFrames) {
    if (_dev.dumpDir.empty() || f < firstStep || f >= _engine.frameCount()) continue;
    char name[32];
    std::snprintf(name, sizeof(name), "state_%05llu.json", static_cast<unsigned long long>(f));
    dumpState(name);
  }
}

void AgentRun::assetsReloaded() {
  if (!_recorder) return;
  // A replay needs the files the play ran with: the recording ends here, replayable up to now.
  const auto last = _engine.stateJson(false);
  _recorder->end(&last);
  _recorder.reset();
  _notRecordingNotice = "No marker: the recording ended at a reload. Play again to record.";
  JM_LOG_INFO("[Session] the recorded play ends at this reload");
}

void AgentRun::shutdown() {
  if (_recorder && std::uncaught_exceptions() > 0) {
    _recorder->end();  // a crash unwinding: the state may be half made, and a throw here would abort
  } else if (_recorder) {
    const auto last = _engine.stateJson(false);
    _recorder->end(&last);
  }
}

void AgentRun::describeState(nlohmann::json& state) {
  for (nlohmann::json& entity : state["entities"]) {
    const nlohmann::json& id = entity["id"];
    const auto origin = _origins.find(EntityId{id[0].get<uint32_t>(), id[1].get<uint32_t>()});
    if (origin != _origins.end()) entity["origin"] = origin->second;
  }
  if (_held) {
    nlohmann::json picked = nlohmann::json::array();
    for (const Pick& pick : picks()) picked.push_back(pickJson(pick));
    state["moment"] = {{"picks", std::move(picked)}};
  }
  if (!_playback) return;
  state["replay"] = {{"frames", _playback->frames()},
                     {"diverged", _divergedAt ? nlohmann::json(*_divergedAt) : nlohmann::json()}};
}

bool AgentRun::devicesMuted() const {
  return _dev.drive || !_dev.inputReplay.empty() || _held || replayingAt(_engine.frameCount());
}

bool AgentRun::fastForwarding() const {
  return _dev.afterReplay == DevOptions::AfterReplay::Live && replayingAt(_engine.frameCount());
}

std::optional<bool> AgentRun::windowFocused() const {
  const uint64_t frame = _engine.frameCount();
  if (!replayingAt(frame)) return std::nullopt;
  return _playback->focusedAt(frame);
}

bool AgentRun::replayingAt(uint64_t frame) const {
  return _playback && _playback->covers(frame) && (!_dev.playUntil || frame < *_dev.playUntil);
}

void AgentRun::replay(const std::vector<nlohmann::json>& events) {
  EventBus& bus = _engine.getEventBus();
  // Floats went through JSON as doubles: they come back bit for bit.
  for (const nlohmann::json& e : events) {
    const std::string type = e.value("type", "");
    if (type == "move") bus.emit(EVT_MouseMove, events::MouseMove{e.value("x", 0.0f), e.value("y", 0.0f)});
    else if (type == "button") bus.emit(EVT_MouseButton, events::MouseButton{e.value("button", 0), e.value("down", false)});
    else if (type == "wheel") bus.emit(EVT_MouseWheel, events::MouseWheel{e.value("dx", 0.0f), e.value("dy", 0.0f)});
    else if (type == "resize") bus.emit(EVT_WindowResize, events::WindowResized{e.value("w", 0), e.value("h", 0)});
    else if (type == "set" || type == "scene") giveInput(e);
    else if (type == "key") {
      events::NamedKey key{{}, e.value("down", false)};
      const std::string name = e.value("name", "");
      name.copy(key.name, sizeof(key.name) - 1);
      bus.emit(EVT_NamedKey, key);
    }
  }
}

void AgentRun::giveInput(const nlohmann::json& event) {
  // Done, and recorded, the same either way.
  if (event.value("type", "") == "set") _engine.getSession().setJson(event.value("key", ""), event.value("value", nlohmann::json()));
  else _engine.getSceneManager().loadScene(event.value("path", ""));
  recordInput(event);
}

void AgentRun::recordInput(nlohmann::json event) {
  if (_recorder) _recorder->input(std::move(event));  // gone once a reload ended the recording
}

void AgentRun::gamepadUsed() {
  if (_recorder) _recorder->gamepadUsed();
}

int AgentRun::dropMarker(const std::string& note) { return mark(note, false); }

void AgentRun::noteError(nlohmann::json error) {
  if (_dev.drive) _driverErrors.push_back(std::move(error));
}

void AgentRun::dumpState(const std::string& name) {
  if (_dev.dumpDir.empty()) return;
  std::error_code ec;
  std::filesystem::create_directories(_dev.dumpDir, ec);
  const auto path = _dev.dumpDir / name;
  std::string error;  // one line, as the driver answers: greppable
  if (platform::writeAtomically(path, _engine.stateJson().dump() + "\n", error)) JM_LOG_INFO("[Engine] state dumped to {}", path.string());
  else JM_LOG_ERROR("[Engine] {}", error);
}
