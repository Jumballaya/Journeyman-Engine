import { Overrides, TileMap, spawn } from "@jm/runtime";

// Puts the level's cables and sentries where its map marks them.
const map = TileMap.find("Map");
const cables = map.objects("cable");
for (let i = 0; i < cables.length; i++) {
  const v = cables[i];
  spawn("cable", v.x, v.y, new Overrides().param("index", i).param("length", v.properties.get("length").number(200)));
}
const sentries = map.objects("sentry");
for (let i = 0; i < sentries.length; i++) spawn("sentry", sentries[i].x, sentries[i].y + 20);
