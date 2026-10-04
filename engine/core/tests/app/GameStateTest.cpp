#include <gtest/gtest.h>

#include "../assets/TempDir.hpp"
#include "GameState.hpp"

TEST(GameState, NumbersAndStringsRoundTrip) {
  GameState s;
  s.setNumber("score", 1200);
  s.setString("stage", "level2");
  EXPECT_DOUBLE_EQ(s.getNumber("score", 0), 1200);
  EXPECT_EQ(s.getString("stage").value(), "level2");
  EXPECT_DOUBLE_EQ(s.getNumber("missing", 7), 7);
  EXPECT_FALSE(s.getString("score").has_value());  // wrong type → absent
  s.remove("score");
  EXPECT_FALSE(s.has("score"));
}

TEST(GameState, FileBackedStorePersistsAcrossInstances) {
  TempDir dir;
  const auto file = dir.path() / "nested" / "save.json";
  {
    GameState s(file);
    s.setNumber("hiscore", 98765);
    s.flush();
  }
  GameState reloaded(file);
  EXPECT_DOUBLE_EQ(reloaded.getNumber("hiscore", 0), 98765);
}

TEST(GameState, CorruptFileIsIgnored) {
  TempDir dir;
  dir.writeFile("save.json", "{not json");
  GameState s(dir.path() / "save.json");
  EXPECT_FALSE(s.has("anything"));
}
