// Endless parallax background. Param "theme": ocean | storm | land.
// A grid of wrapping tiles forms the ground; islands, land props and clouds
// spawn above the screen and drift down past it.
import { tileGrid, Timer, Random, Overrides, Params, Renderer, spawn } from "@jm/runtime";
import { HALF_W } from "./lib/util";

const FX = "assets/atlases/fx.atlas.json#";
const SHMUP = "assets/atlases/shmup.atlas.json#";

const LAND_PROPS = [
  "tile_0048", "tile_0048", "tile_0060", "tile_0060", "tile_0036", "tile_0072", "tile_0084",
  "tile_0078", "tile_0090", "tile_0096", "tile_0102", "tile_0116", "tile_0116", "tile_0056",
  "tile_0054", "tile_0066", "tile_0109", "tile_0111", "tile_0117",
];
const ISLAND_W: f32[] = [56, 40, 72, 30];
const ISLAND_H: f32[] = [44, 32, 52, 26];

const theme = Params.text("theme", "ocean");
const land = theme == "land";
const storm = theme == "storm";
const speed = <f32>Params.number("speed", land ? 60 : 40);
const decorTimer = new Timer();
const cloudTimer = new Timer();

// Ground layers; land is dimmed so planes, shots and the HUD stand out.
function ground(texture: string): Overrides {
  const o = new Overrides().texture(texture).velocity(0, -speed);
  return land ? o.tint(0.62, 0.7, 0.6) : o;
}

function spawnIsland(y: f32): void {
  const i = Random.int(0, 3);
  spawn("bg_decor", Random.range(-HALF_W + 30, HALF_W - 30), y,
        ground(FX + "island_" + i.toString()).scale(ISLAND_W[i], ISLAND_H[i]));
}

function spawnLandProp(y: f32): void {
  spawn("bg_decor", -224 + <f32>Random.int(0, 14) * 32, y, ground(SHMUP + Random.pick(LAND_PROPS)).scale(16, 16));
}

function spawnCloud(y: f32): void {
  const k = Random.int(0, 2);
  const w: f32 = k == 0 ? 64 : k == 1 ? 48 : 80;
  const h: f32 = k == 0 ? 32 : k == 1 ? 24 : 36;
  const scale = Random.range(1.0, 1.6);
  const o = new Overrides().texture(FX + "cloud_" + k.toString())
    .scale(w * scale, h * scale).velocity(0, -speed * Random.range(2.4, 3.2));
  spawn("cloud", Random.range(-HALF_W, HALF_W), y, storm ? o.tint(0.55, 0.6, 0.7, 0.7) : o.tint(1, 1, 1, 0.5));
}

if (land) {
  Renderer.setClearColor(0.443, 0.585, 0.141);
  // 32px grass grid, 15 x 22 tiles, wrapping every 704px.
  tileGrid("bg_tile", { columns: 15, rows: 22, x: -224, y: -336, width: 32, height: 32 },
           ground(SHMUP + "tile_0110").scale(16, 16).scrollY(-368, 336));
  for (let y: f32 = -320; y < 360; y += 48) spawnLandProp(y);
} else {
  if (storm) Renderer.setClearColor(0.114, 0.231, 0.361);
  else Renderer.setClearColor(0.165, 0.435, 0.69);
  // 64px water grid, 8 x 11 tiles, wrapping every 704px.
  tileGrid("bg_tile", { columns: 8, rows: 11, x: -224, y: -352, width: 64, height: 64 },
           ground(FX + (storm ? "water_storm" : "water")).scale(32, 32).scrollY(-384, 320));
  if (!storm) {
    spawnIsland(Random.range(-200, 0));
    spawnIsland(Random.range(120, 300));
  }
}
spawnCloud(Random.range(-200, 300));

export function onUpdate(dt: f32): void {
  decorTimer.tick(dt);
  if (decorTimer.ready && !storm) {
    decorTimer.start(land ? Random.range(0.45, 0.9) : Random.range(4.0, 7.5));
    if (land) spawnLandProp(380);
    else spawnIsland(400);
  }
  cloudTimer.tick(dt);
  if (cloudTimer.ready) {
    cloudTimer.start(storm ? Random.range(0.8, 1.8) : Random.range(2.0, 4.0));
    spawnCloud(Random.range(380, 440));
  }
}
