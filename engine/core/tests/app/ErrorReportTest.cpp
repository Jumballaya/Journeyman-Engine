#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "../assets/TempDir.hpp"
#include "ErrorReport.hpp"

TEST(ErrorReport, CategoryComesFromTheTagAndTheSourceWhenKnown) {
  const nlohmann::json e = ErrorReport::toJson(LogLevel::Error, "[Script] hero.ts aborted: hp < 0",
                                               ErrorSource{"assets/scripts/hero.ts", 12, 5}, 240);
  EXPECT_EQ(e, (nlohmann::json{{"level", "error"}, {"category", "Script"}, {"message", "hero.ts aborted: hp < 0"},
                               {"file", "assets/scripts/hero.ts"}, {"line", 12}, {"column", 5}, {"frame", 240}}));
}

TEST(ErrorReport, AnUntaggedMessageWithoutASourceIsJustTheMessage) {
  const nlohmann::json e = ErrorReport::toJson(LogLevel::Critical, "Fatal: out of memory", {}, 0);
  EXPECT_EQ(e, (nlohmann::json{{"level", "critical"}, {"message", "Fatal: out of memory"}, {"frame", 0}}));
}

TEST(ErrorReport, WritesOneJsonLinePerErrorAndCountsThem) {
  TempDir dir;
  const auto path = dir.path() / "errors.jsonl";
  {
    ErrorReport report(path.string());
    report.add(LogLevel::Error, "[UI] document 'a.ui.html' failed to load", ErrorSource{"assets/ui/a.ui.html"}, 3);
    report.add(LogLevel::Error, "[Audio] unknown sound 'boom'", {}, 9);
    EXPECT_EQ(report.count(), 2u);
    EXPECT_EQ(report.first(), "[UI] document 'a.ui.html' failed to load");
  }
  std::ifstream in(path);
  std::string first, second, extra;
  std::getline(in, first);
  std::getline(in, second);
  EXPECT_EQ(nlohmann::json::parse(first)["file"], "assets/ui/a.ui.html");
  EXPECT_EQ(nlohmann::json::parse(second)["frame"], 9);
  EXPECT_FALSE(std::getline(in, extra));
}

// With no destination it still counts: JM_STRICT works without JM_ERRORS.
TEST(ErrorReport, CountsWithNowhereToWrite) {
  ErrorReport report("");
  report.add(LogLevel::Error, "[Engine] failed", {}, 0);
  EXPECT_EQ(report.count(), 1u);
}
