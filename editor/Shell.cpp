#include "Shell.hpp"

#include <regex>

namespace shell {

std::string quotePosix(std::string_view arg) {
  std::string out = "'";
  for (char c : arg) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

std::string quoteWindows(std::string_view arg) {
  std::string out = "\"";
  size_t backslashes = 0;
  for (char c : arg) {
    if (c == '\\') {
      ++backslashes;
      continue;
    }
    // Backslashes count only before a quote, where each must be doubled.
    out.append(c == '"' ? backslashes * 2 : backslashes, '\\');
    backslashes = 0;
    out += c == '"' ? std::string("\"\"") : std::string(1, c);
  }
  out.append(backslashes * 2, '\\');  // before the closing quote
  return out + "\"";
}

std::string quote(std::string_view arg) {
#ifdef _WIN32
  return quoteWindows(arg);
#else
  return quotePosix(arg);
#endif
}

LogBook::Level levelOf(std::string_view line) {
  // Whole words, so "error_screen.ui.html" and "0 errors" don't count.
  static const std::regex error(R"((^|[^\w])(error|errors|failed|failure|fatal)\b)", std::regex::icase);
  static const std::regex none(R"(\b(0|no) errors\b)", std::regex::icase);
  static const std::regex warning(R"((^|[^\w])(warning|warn)\b)", std::regex::icase);
  const std::string text(line);
  if (std::regex_search(text, error) && !std::regex_search(text, none)) return LogBook::Level::Error;
  if (std::regex_search(text, warning)) return LogBook::Level::Warning;
  return LogBook::Level::Info;
}

}  // namespace shell
