#pragma once

#include <string>

#include "Project.hpp"

// JSON in the one layout the editor and jm share (cli/internal/jsonfmt
// documents it): two-space indent, keys in their order, short arrays of
// scalars on one line, numbers rounded to six decimals with whole ones as
// integers, and a final newline. Both are tested against the same files, so
// a file reads the same whoever wrote it and a small edit is a small diff.
std::string formatJson(const Json& value);
