#include "EditorSession.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <utility>

#include "core/app/Platform.hpp"
#include "core/logger/LogMacros.hpp"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

long long unixSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

long currentProcessId() {
#ifdef _WIN32
  return _getpid();
#else
  return getpid();
#endif
}

}  // namespace

EditorSession::EditorSession(const fs::path& projectRoot)
    : _file(projectRoot / ".jm" / ("editor-session-" + std::to_string(currentProcessId()) + ".json")) {
  // The jm runs started here inherit it and skip this file: the person knows their own edits.
  const std::string pid = std::to_string(currentProcessId());
#ifdef _WIN32
  _putenv_s("JM_EDITOR_PID", pid.c_str());
#else
  setenv("JM_EDITOR_PID", pid.c_str(), 1);
#endif
}

EditorSession::~EditorSession() {
  std::error_code ec;
  fs::remove(_file, ec);
  fs::remove(inbox(), ec);
}

fs::path EditorSession::inbox() const {
  fs::path in = _file;
  return in.replace_extension(".in");
}

void EditorSession::publish(const View& view) {
  const Json session = {{"open", view.open}, {"unsaved", view.unsaved}, {"scene", view.scene}, {"selection", view.selection}, {"notes", _notes}};
  const auto now = std::chrono::steady_clock::now();
  if (session == _published && now - _publishedAt < std::chrono::seconds(5)) return;
  _published = session;
  _publishedAt = now;
  Json file = session;
  file["updated"] = unixSeconds();
  std::string error;
  if (!platform::writeAtomically(_file, file.dump(2), error) && !std::exchange(_writeFailed, true)) {
    JM_LOG_WARN("[Editor] session: {}", error);
  }
}

void EditorSession::pointOut(const std::string& scene, const std::vector<std::string>& entities, const std::string& text) {
  static int lastId = 0;  // the editor's, not this session's: a project opened again mustn't reuse a note's id
  const int id = ++lastId;
  _notes.push_back({{"id", id}, {"at", unixSeconds()}, {"scene", scene}, {"entities", entities}, {"text", text}});
  if (_notes.size() > 20) _notes.erase(_notes.begin());
}

std::vector<EditorSession::Show> EditorSession::takeRequests() {
  std::vector<Show> out;
  std::error_code ec;
  const std::uintmax_t size = fs::file_size(inbox(), ec);
  if (ec || size <= _read) return out;
  std::ifstream in(inbox(), std::ios::binary);
  in.seekg(static_cast<std::streamoff>(_read));
  const std::string chunk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const size_t end = chunk.rfind('\n');
  if (end == std::string::npos) return out;  // a line still being written
  _read += end + 1;
  size_t from = 0;
  for (size_t nl = chunk.find('\n'); from <= end; from = nl + 1, nl = chunk.find('\n', from)) {
    const Json line = Json::parse(chunk.substr(from, nl - from), nullptr, false);
    if (!line.is_object()) continue;
    auto text = [&](const char* key) { return line.contains(key) && line[key].is_string() ? line[key].get<std::string>() : std::string(); };
    out.push_back({text("scene"), text("entity"), text("note")});
  }
  return out;
}
