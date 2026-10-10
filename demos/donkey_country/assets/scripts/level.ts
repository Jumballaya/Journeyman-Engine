import { Overrides, TileMap, spawn } from "@jm/runtime";

// Puts the level's vines and gnawbles where its map marks them.
const map = TileMap.find("Map");
const vines = map.objects("vine");
for (let i = 0; i < vines.length; i++) {
  const v = vines[i];
  spawn("vine", v.x, v.y, new Overrides().param("index", i).param("length", v.properties.get("length").number(200)));
}
const gnawbles = map.objects("gnawble");
for (let i = 0; i < gnawbles.length; i++) spawn("gnawble", gnawbles[i].x, gnawbles[i].y + 20);
