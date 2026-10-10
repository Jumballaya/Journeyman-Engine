#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "Project.hpp"

// <project>/.jm/editor-session-<pid>.json: files open here and those unsaved, for jm's
// warnings. publish() rewrites it on change and every 5 s; the destructor removes it.
class EditorSession {
 public:
  explicit EditorSession(const std::filesystem::path& projectRoot);
  ~EditorSession();
  EditorSession(const EditorSession&) = delete;
  EditorSession& operator=(const EditorSession&) = delete;

  void publish(const std::vector<std::string>& open, const std::vector<std::string>& unsaved);
  const std::filesystem::path& file() const { return _file; }

 private:
  std::filesystem::path _file;
  Json _published;
  std::chrono::steady_clock::time_point _publishedAt{};
  bool _writeFailed = false;  // logged once
};
