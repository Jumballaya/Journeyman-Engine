#include "EditorSession.hpp"

#include <utility>

#include "core/logger/LogMacros.hpp"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

long currentProcessId() {
#ifdef _WIN32
  return _getpid();
#else
  return getpid();
#endif
}

}  // namespace

EditorSession::EditorSession(const fs::path& projectRoot)
    : _file(projectRoot / ".jm" / ("editor-session-" + std::to_string(currentProcessId()) + ".json")) {}

EditorSession::~EditorSession() {
  std::error_code ec;
  fs::remove(_file, ec);
}

void EditorSession::publish(const std::vector<std::string>& open, const std::vector<std::string>& unsaved) {
  const Json session = {{"open", open}, {"unsaved", unsaved}};
  const auto now = std::chrono::steady_clock::now();
  if (session == _published && now - _publishedAt < std::chrono::seconds(5)) return;
  _published = session;
  _publishedAt = now;
  Json file = session;
  file["updated"] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::string error;
  if (!writeAtomically(_file, file.dump(2), error) && !std::exchange(_writeFailed, true)) {
    JM_LOG_WARN("[Editor] session: {}", error);
  }
}
