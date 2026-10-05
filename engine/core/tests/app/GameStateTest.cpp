#include <gtest/gtest.h>

#include <limits>

#include "../assets/TempDir.hpp"
#include "EntityStores.hpp"
#include "GameState.hpp"
#include "World.hpp"

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

#include "GameClock.hpp"

TEST(GameClock, InvalidScalePauses) {
  GameClock clock;
  clock.setScale(std::numeric_limits<float>::quiet_NaN());
  EXPECT_TRUE(clock.paused());
  clock.setScale(std::numeric_limits<float>::infinity());
  EXPECT_TRUE(clock.paused());
  clock.setScale(0.5f);
  clock.advance(0.1f);
  EXPECT_FLOAT_EQ(clock.dt(), 0.05f);
}

TEST(GameState, JsonValuesAndKeys) {
  GameState state;
  state.setJson("party", nlohmann::json::array({"kael", "lyra"}));
  state.setNumber("party.gold", 40);
  state.setString("name", "x");
  EXPECT_EQ(state.getJson("party"), nlohmann::json::array({"kael", "lyra"}));
  EXPECT_FALSE(state.getJson("missing").has_value());
  EXPECT_EQ(state.keys("party"), (std::vector<std::string>{"party", "party.gold"}));
  EXPECT_EQ(state.keys().size(), 3u);
}

TEST(EntityStores, OneStorePerEntityDroppedWithIt) {
  World world;
  EntityStores stores;
  EntityId a = world.createEntity(), b = world.createEntity();
  const int32_t idA = stores.idFor(a);
  EXPECT_GE(idA, EntityStores::kFirstId);
  EXPECT_EQ(stores.idFor(a), idA);
  EXPECT_NE(stores.idFor(b), idA);
  stores.find(idA)->setNumber("hp", 3);
  world.destroyEntity(a);
  stores.prune(world);
  EXPECT_EQ(stores.find(idA), nullptr);
  EXPECT_NE(stores.find(stores.idFor(b)), nullptr);
}
