#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Project.hpp"

// <project>/.jm/editor-session-<pid>.json: what's open here (files, the scene and
// its selection), what's unsaved, and the notes the person pointed out to their
// agent, for jm (warnings, editor_state). JM_EDITOR_PID tells this editor's own
// jm runs to skip it. publish() rewrites it on change and every 5 s; the
// destructor removes it. The agent asks to be shown things through
// editor-session-<pid>.in beside it: one JSON object a line (see Show).
class EditorSession {
 public:
  struct View {
    std::vector<std::string> open, unsaved;
    std::string scene;                   // the scene being edited, "" for none
    std::vector<std::string> selection;  // its selected entities' names
  };
  // The agent asks: open scene (if given), select and frame entity (by name), say note.
  struct Show {
    std::string scene, entity, note;
  };

  explicit EditorSession(const std::filesystem::path& projectRoot);
  ~EditorSession();
  EditorSession(const EditorSession&) = delete;
  EditorSession& operator=(const EditorSession&) = delete;

  void publish(const View& view);
  // A note for the agent about entities of scene; the last 20 are kept in the session.
  void pointOut(const std::string& scene, const std::vector<std::string>& entities, const std::string& text);
  // The agent's requests since the last call (lines that aren't one are skipped).
  std::vector<Show> takeRequests();
  const std::filesystem::path& file() const { return _file; }
  std::filesystem::path inbox() const;

 private:
  std::filesystem::path _file;
  Json _published;
  Json _notes = Json::array();
  std::chrono::steady_clock::time_point _publishedAt{};
  std::uintmax_t _read = 0;  // bytes of the inbox taken
  bool _writeFailed = false;  // logged once
};
