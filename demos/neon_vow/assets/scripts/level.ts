import { GameState, Overrides, TileMap, spawn } from "@jm/runtime";

// Puts props and painted light sources where the map marks them.
const map = TileMap.find("Map");
const lights = map.objects("light");
for (let i = 0; i < lights.length; i++) {
  const light = lights[i];
  spawn(light.properties.get("prefab").text(), light.x, light.y,
    new Overrides().param("phase", light.properties.get("phase").number()));
}
const cables = map.objects("cable");
for (let i = 0; i < cables.length; i++) {
  const v = cables[i];
  spawn("cable", v.x, v.y, new Overrides().param("index", i).param("length", v.properties.get("length").number(200)));
}
const sentries = map.objects("sentry");
for (let i = 0; i < sentries.length; i++) {
  const s = sentries[i];
  spawn("sentry", s.x, s.y + 20, new Overrides().param("range", s.properties.get("range").number(160)));
}

const pods = map.objects("pod");
for (let i = 0; i < pods.length; i++) {
  spawn("pod", pods[i].x, pods[i].y, new Overrides().tag("pod-" + i.toString()));
}
const shards = map.objects("shard");
GameState.setNumber("shardTotal", shards.length);
for (let i = 0; i < shards.length; i++) spawn("shard", shards[i].x, shards[i].y);

const rail = map.objects("rail")[0];
spawn("cart", rail.points[0], rail.points[1]);
const hazards = map.objects("rail-hazard");
for (let i = 0; i < hazards.length; i++) {
  const h = hazards[i];
  spawn(h.properties.get("gap").bool() ? "rail_arc" : "rail_sentry", h.x, h.y);
}
