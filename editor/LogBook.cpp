#include "LogBook.hpp"

#include <chrono>
#include <regex>

#include <spdlog/sinks/callback_sink.h>

#include "core/logger/LoggerService.hpp"

namespace {

const auto kStart = std::chrono::steady_clock::now();
constexpr size_t kMaxEntries = 5000;

// "assets/scripts/player.ts:42" or "assets/scripts/player.ts(42,7)" anywhere in the text.
void findLocation(LogBook::Entry& entry) {
  static const std::regex location(R"(((?:assets|scenes)/[\w./-]+\.\w+)(?:[:(](\d+))?)");
  std::smatch match;
  if (!std::regex_search(entry.text, match, location)) return;
  entry.file = match[1].str();
  if (match[2].matched) entry.line = std::stoi(match[2].str());
}

}  // namespace

void LogBook::add(Level level, Source source, std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  if (text.empty()) return;
  std::lock_guard lock(_mutex);
  ++_version;
  if (!_entries.empty() && _entries.back().text == text && _entries.back().source == source) {
    ++_entries.back().repeats;
    _entries.back().time = std::chrono::duration<double>(std::chrono::steady_clock::now() - kStart).count();
    return;
  }
  Entry entry{level, source, std::move(text),
              std::chrono::duration<double>(std::chrono::steady_clock::now() - kStart).count()};
  findLocation(entry);
  ++_counts[static_cast<int>(level)];
  _entries.push_back(std::move(entry));
  if (_entries.size() > kMaxEntries) {
    --_counts[static_cast<int>(_entries.front().level)];
    _entries.erase(_entries.begin(), _entries.begin() + kMaxEntries / 10);
  }
}

void LogBook::clear() {
  std::lock_guard lock(_mutex);
  _entries.clear();
  _counts = {};
  ++_version;
}

std::vector<LogBook::Entry> LogBook::snapshot() const {
  std::lock_guard lock(_mutex);
  return _entries;
}

uint64_t LogBook::version() const {
  std::lock_guard lock(_mutex);
  return _version;
}

std::array<int, 3> LogBook::counts() const {
  std::lock_guard lock(_mutex);
  return _counts;
}

LogBook& LogBook::instance() {
  static LogBook book;
  return book;
}

namespace {

// Engine bookkeeping (modules starting and stopping as the preview restarts)
// that would bury the game's own lines.
bool lifecycleNoise(std::string_view text) {
  for (std::string_view prefix : {"[ModuleRegistry]", "[Archive]", "[JSON]", "[Engine] Shutting down", "Journeyman Engine"}) {
    if (text.starts_with(prefix)) return true;
  }
  for (std::string_view suffix : {"] initialized", "] shutdown"}) {
    if (text.ends_with(suffix)) return true;
  }
  return false;
}

}  // namespace

void captureEngineLog() {
  auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg& msg) {
    if (msg.level < spdlog::level::info) return;
    const std::string_view text(msg.payload.data(), msg.payload.size());
    if (msg.level == spdlog::level::info && lifecycleNoise(text)) return;
    const LogBook::Level level = msg.level >= spdlog::level::err    ? LogBook::Level::Error
                                 : msg.level == spdlog::level::warn ? LogBook::Level::Warning
                                                                    : LogBook::Level::Info;
    LogBook::instance().add(level, LogBook::Source::Engine, std::string(msg.payload.begin(), msg.payload.end()));
  });
  LoggerService::instance().addSink(sink);
}
