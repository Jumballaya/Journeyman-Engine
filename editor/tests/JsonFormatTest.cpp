#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "JsonFormat.hpp"

// The same fixtures as jm's writer (cli/internal/jsonfmt/testdata): both must
// write the same bytes.
TEST(JsonFormat, MatchesJmsWriter) {
  int checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator(JM_JSONFMT_TESTDATA)) {
    const std::string in = entry.path().string();
    if (!in.ends_with(".in.json")) continue;
    auto read = [](const std::string& path) {
      std::ifstream f(path);
      std::stringstream text;
      text << f.rdbuf();
      return text.str();
    };
    const std::string want = read(in.substr(0, in.size() - 8) + ".out.json");
    const std::string got = formatJson(Json::parse(read(in)));
    EXPECT_EQ(got, want) << in;
    EXPECT_EQ(formatJson(Json::parse(got)), got) << in << ": formatting the output changes it";
    ++checked;
  }
  EXPECT_GT(checked, 0);
}
