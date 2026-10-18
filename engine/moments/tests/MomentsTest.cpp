#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "../../core/tests/assets/TempDir.hpp"
#include "Engine.hpp"
#include "../NoteEdit.hpp"
#include "../PickTags.hpp"

// A pick's number sits at its mark, off it; the focused label's line to its
// pick ends on both.
TEST(Moments, NumbersSitAtTheirPicks) {
  using Kind = AgentRun::Pick::Kind;
  const glm::vec2 screen(256, 224), tag(12, 12);
  // An outline: on its top-left corner, above it; below it at the screen's top.
  EXPECT_EQ(pick_tags::numberAt(Kind::Entity, {50, 100}, {70, 120}, tag, screen), glm::vec2(50, 87));
  EXPECT_EQ(pick_tags::numberAt(Kind::Entity, {50, 5}, {70, 25}, tag, screen), glm::vec2(50, 26));
  // An area: inside its corner. A point: up-left of its ring, down-right in the screen's top-left corner.
  EXPECT_EQ(pick_tags::numberAt(Kind::Area, {50, 100}, {150, 200}, tag, screen), glm::vec2(52, 102));
  EXPECT_EQ(pick_tags::numberAt(Kind::Point, {90, 90}, {110, 110}, tag, screen), glm::vec2(84, 84));
  EXPECT_EQ(pick_tags::numberAt(Kind::Point, {-7, -7}, {13, 13}, tag, screen), glm::vec2(7, 7));
  // Off to the side and above: the line goes from the tag's corner to the box's.
  const auto aside = pick_tags::leader({10, 10, 40, 20}, {100, 100}, {120, 120});
  EXPECT_EQ(aside[0], glm::vec2(50, 30));
  EXPECT_EQ(aside[1], glm::vec2(100, 100));
  // Straight above: down from the tag's bottom to the box's top, where they overlap.
  const auto above = pick_tags::leader({90, 10, 40, 20}, {100, 100}, {120, 120});
  EXPECT_EQ(above[0], glm::vec2(110, 30));
  EXPECT_EQ(above[1], glm::vec2(110, 100));
}

// A note typed while held: UTF-8 a code point at a time, capped; commit
// keeps it, cancel gives back what it was.
TEST(Moments, NotesAreTypedAndKeptOrUndone) {
  NoteEdit edit;
  edit.type(U'x');  // not editing: nothing
  EXPECT_EQ(edit.text(), "");
  edit.begin("gap");
  for (const char32_t c : {U' ', U'\u00e9', U'\u2192', U'\U0001F600', U'\n', U'\x7f', char32_t(0xD800)}) edit.type(c);
  EXPECT_EQ(edit.text(), "gap \xC3\xA9\xE2\x86\x92\xF0\x9F\x98\x80");  // controls and a lone surrogate aren't kept
  edit.erase();  // the emoji: all four bytes
  edit.erase();
  EXPECT_EQ(edit.text(), "gap \xC3\xA9");
  EXPECT_EQ(edit.cancel(), "gap");  // Esc: as it was
  EXPECT_FALSE(edit.editing());
  edit.begin(std::string("ok\xFF", 3));  // bad bytes read as U+FFFD
  EXPECT_EQ(edit.text(), "ok\xEF\xBF\xBD");
  for (int i = 0; i < 300; ++i) edit.type(U'\u00e9');
  std::string capped = "ok\xEF\xBF\xBD";
  for (size_t i = 3; i < NoteEdit::kLength; ++i) capped += "\xC3\xA9";
  EXPECT_EQ(edit.text(), capped);  // 200 code points
  for (size_t i = 0; i < NoteEdit::kLength + 5; ++i) edit.erase();
  EXPECT_EQ(edit.commit(), "");
  EXPECT_EQ(edit.text(), "");
}

// A held run's picks through the driver, in a 640x400 frame showing a
// 320x180 game (scale 2, 20 px bars above and below), its camera at (64, 32)
// zoomed 2x: logical (x, y) is the world's (64 + (x - 160) / 2, 32 + (90 - y) / 2).
// The map: 8x4 cells of 16, at the origin; ground along the bottom row and at (3, 1),
// and a layer moved past its right edge with ground at (0, 0). A stick, turned,
// near the area's corner but not in it.
TEST(Moments, PointsAndAreasSayWhatsThere) {
  TempDir game;
  game.writeFile("tiles.tsj", R"({"type": "tileset", "tilewidth": 16, "tileheight": 16, "tilecount": 1, "columns": 1,
                                  "tiles": [{"id": 0, "type": "ground"}]})");
  game.writeFile("level.tmj", R"({"width": 8, "height": 4, "tilewidth": 16, "tileheight": 16,
      "tilesets": [{"firstgid": 1, "source": "tiles.tsj"}],
      "layers": [{"type": "tilelayer", "name": "ground", "width": 8, "height": 4,
                  "data": [0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,1,0,0,0,0, 1,1,1,1,1,1,1,1]},
                 {"type": "tilelayer", "name": "beyond", "offsetx": 128, "width": 8, "height": 4,
                  "data": [0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0]}]})");
  game.writeFile(".jm.json", R"({"name": "T", "entryScene": "main.scene.json", "scenes": ["main.scene.json"],
      "assets": ["level.tmj", "tiles.tsj"],
      "config": {"window": {"width": 640, "height": 400}, "renderer": {"logicalWidth": 320, "logicalHeight": 180}}})");
  game.writeFile("main.scene.json", R"({"name": "main", "entities": [
      {"name": "Map", "components": {"TransformComponent": {"position": [0, 0, 0]}, "TileMapComponent": {"map": "level.tmj"}}},
      {"name": "Eye", "components": {"TransformComponent": {"position": [64, 32, 0]}, "CameraComponent": {"zoom": 2, "smoothing": 0}}},
      {"name": "Stick", "components": {"TransformComponent": {"position": [34, 56, 0], "scale": [10, 1], "rotation": 0.785398}, "SpriteComponent": {}}},
      {"name": "Coin", "components": {"TransformComponent": {"position": [100, 40, 1], "scale": [4, 4]}, "SpriteComponent": {}}}]})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.renderer = "none";
  options.dev.drive = true;
  options.dev.recordDir = game.path() / "play";
  options.dev.saveDir = game.path() / "save";
  Engine engine(game.path(), ".jm.json", options);
  engine.initialize();
  // A point on (56, 24): ground (3, 1); the coin; an area from (40, 50) to (110, 10); shift
  // near the point takes it away, and on empty ground again puts it back; a point on (136, 8).
  std::istringstream in("step 2\nfreeze\npick 144 106\npick 232 74 add\narea 112 54 252 134 add\n"
                        "pick 146 107 add\npick 144 106 add\npick 304 138 add\n"
                        "note 1\tthis one clips\nnote too dark here\nnote 9 nothing\nnote 4\nsave\n");
  std::ostringstream out;
  engine.agent().drive(in, out);
  std::vector<nlohmann::json> replies;
  for (std::istringstream lines(out.str()); !lines.eof();) {
    std::string line;
    if (std::getline(lines, line) && !line.empty()) replies.push_back(nlohmann::json::parse(line));
  }
  ASSERT_EQ(replies.size(), 14u) << out.str();  // "ready", then one per command
  EXPECT_EQ(replies[3]["picks"], nlohmann::json::parse(R"([{"kind": "point", "at": [56.0, 24.0]}])"));
  EXPECT_EQ(replies[4]["picks"][1]["kind"], "entity");
  EXPECT_EQ(replies[5]["picks"][2], nlohmann::json::parse(R"({"kind": "area", "area": [40.0, 10.0, 110.0, 50.0]})"));
  EXPECT_EQ(replies[6]["picks"].size(), 2u);
  EXPECT_EQ(replies[7]["picks"].size(), 3u);
  EXPECT_EQ(replies[9]["picks"][0]["note"], "this one clips");
  EXPECT_EQ(replies[10]["note"], "too dark here");
  EXPECT_EQ(replies[11]["ok"], false);  // there's no pick 9
  EXPECT_FALSE(replies[12]["picks"][3].contains("note"));
  EXPECT_EQ(replies[13]["picks"], 4);
  engine.shutdown();

  std::ifstream meta(game.path() / "play" / "session.json");
  const nlohmann::json marker = nlohmann::json::parse(meta)["markers"][0];
  EXPECT_EQ(marker["note"], "too dark here");
  const nlohmann::json& picks = marker["picks"];
  ASSERT_EQ(picks.size(), 4u);
  EXPECT_EQ(picks[0]["note"], "this one clips");
  EXPECT_FALSE(picks[1].contains("note"));
  const nlohmann::json& area = picks[1];
  // Its picture's rect: logical (112, 54)-(252, 134), in the frame (224, 128)-(504, 288).
  EXPECT_EQ(area["rect"], (nlohmann::json{0.35, 0.32, 0.4375, 0.4}));
  ASSERT_EQ(area["entities"].size(), 1u);
  EXPECT_EQ(area["entities"][0]["name"], "Coin");
  ASSERT_EQ(area["tiles"].size(), 1u);
  const nlohmann::json& tiles = area["tiles"][0];
  EXPECT_EQ(tiles["map"], "level.tmj");
  EXPECT_EQ(tiles["from"], (nlohmann::json{2, 0}));
  EXPECT_EQ(tiles["to"], (nlohmann::json{6, 3}));
  ASSERT_EQ(tiles["layers"].size(), 1u);
  EXPECT_EQ(tiles["layers"][0]["count"], 6);
  EXPECT_EQ(tiles["layers"][0]["types"], (nlohmann::json{{"ground", 6}}));
  const nlohmann::json& point = picks[2];
  EXPECT_EQ(point["kind"], "point");
  EXPECT_EQ(point["scene"], "main.scene.json");
  EXPECT_EQ(point["rect"], (nlohmann::json{0.45, 0.58, 0.0, 0.0}));
  EXPECT_TRUE(point["entities"].empty());
  EXPECT_EQ(point["tiles"][0]["layers"][0]["cells"], nlohmann::json::parse("[[3, 1, 1]]"));
  // Off the map, on its moved layer: that layer's cell, and no cell of the map's own.
  const nlohmann::json& beyond = picks[3]["tiles"];
  ASSERT_EQ(beyond.size(), 1u);
  EXPECT_FALSE(beyond[0].contains("from"));
  EXPECT_EQ(beyond[0]["layers"], nlohmann::json::parse(R"([{"layer": "beyond", "cells": [[0, 0, 1]], "count": 1, "types": {"ground": 1}}])"));
}
