#pragma once

#include <array>
#include <mutex>
#include <string>
#include <vector>

// Everything the console shows: engine log lines and build output, from any
// thread. Repeats of the previous line fold into a count.
class LogBook {
 public:
  enum class Level { Info, Warning, Error };
  enum class Source { Engine, Build, Editor };
  struct Entry {
    Level level;
    Source source;
    std::string text;
    double time;  // seconds since the editor started
    int repeats = 1;
    std::string file;  // "assets/scripts/player.ts" when the line names one
    int line = 0;
  };

  void add(Level level, Source source, std::string text);
  void clear();

  // A copy under the lock, plus a version that changes on every add/clear.
  std::vector<Entry> snapshot() const;
  uint64_t version() const;
  std::array<int, 3> counts() const;  // by Level

  static LogBook& instance();

 private:
  mutable std::mutex _mutex;
  std::vector<Entry> _entries;
  std::array<int, 3> _counts{};
  uint64_t _version = 0;
};

// Routes the engine's log (spdlog) into the LogBook. Call once.
void captureEngineLog();

// While one exists, engine lines are not captured (an engine the user never sees).
struct MuteEngineLog {
  MuteEngineLog();
  ~MuteEngineLog();
};
