#pragma once

#include <string>
#include <string_view>

#include "LogBook.hpp"

// Talking to `jm` through a shell: quoting its arguments, and reading its output.
namespace shell {

// One argument for sh: always single-quoted.
std::string quotePosix(std::string_view arg);
// One argument for cmd.exe, as the C runtime (and Go's os.Args) split it:
// double-quoted, with backslashes before a quote doubled and quotes written
// as "" (which also keeps cmd's own quote tracking in step).
std::string quoteWindows(std::string_view arg);
// This platform's.
std::string quote(std::string_view arg);

// How a line of jm output reads: an error, a warning or neither. Only for
// showing it; whether the command failed is its exit status.
LogBook::Level levelOf(std::string_view line);

}  // namespace shell
