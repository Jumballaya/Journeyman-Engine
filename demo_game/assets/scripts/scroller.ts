// Endless parallax background. Param "theme": ocean | storm | land.
// A grid of wrapping tiles forms the ground; islands, land props and clouds
// spawn above the screen and drift down past it.
import { Overrides, Params, Renderer, spawn } from "@jm/runtime";
import { HALF_W, pick, rand, randInt } from "./lib/util";

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
let decorTimer: f32 = 0;
let cloudTimer: f32 = 0;

// Ground layers; land is dimmed so planes, shots and the HUD stand out.
function ground(texture: string): Overrides {
  const o = new Overrides().texture(texture).velocity(0, -speed);
  return land ? o.tint(0.62, 0.7, 0.6) : o;
}

function spawnTile(texture: string, scale: f32, x: f32, y: f32, minY: f32, maxY: f32): void {
  spawn("bg_tile", x, y, ground(texture).scale(scale, scale)
    .json("ScrollWrapComponent", "minY", minY.toString())
    .json("ScrollWrapComponent", "maxY", maxY.toString()));
}

function spawnIsland(y: f32): void {
  const i = randInt(0, 3);
  spawn("bg_decor", rand(-HALF_W + 30, HALF_W - 30), y,
        ground(FX + "island_" + i.toString()).scale(ISLAND_W[i], ISLAND_H[i]));
}

function spawnLandProp(y: f32): void {
  spawn("bg_decor", -224 + <f32>randInt(0, 14) * 32, y, ground(SHMUP + pick(LAND_PROPS)).scale(16, 16));
}

function spawnCloud(y: f32): void {
  const k = randInt(0, 2);
  const w: f32 = k == 0 ? 64 : k == 1 ? 48 : 80;
  const h: f32 = k == 0 ? 32 : k == 1 ? 24 : 36;
  const scale = rand(1.0, 1.6);
  const o = new Overrides().texture(FX + "cloud_" + k.toString())
    .scale(w * scale, h * scale).velocity(0, -speed * rand(2.4, 3.2));
  spawn("cloud", rand(-HALF_W, HALF_W), y, storm ? o.tint(0.55, 0.6, 0.7, 0.7) : o.tint(1, 1, 1, 0.5));
}

if (land) {
  Renderer.setClearColor(0.443, 0.585, 0.141);
  // 32px grass grid, 15 x 22 tiles, wrapping every 704px.
  for (let r = 0; r < 22; r++) {
    for (let c = 0; c < 15; c++) spawnTile(SHMUP + "tile_0110", 16, -224 + <f32>c * 32, -336 + <f32>r * 32, -368, 336);
  }
  for (let y: f32 = -320; y < 360; y += 48) spawnLandProp(y);
} else {
  if (storm) Renderer.setClearColor(0.114, 0.231, 0.361);
  else Renderer.setClearColor(0.165, 0.435, 0.69);
  // 64px water grid, 8 x 11 tiles, wrapping every 704px.
  for (let r = 0; r < 11; r++) {
    for (let c = 0; c < 8; c++) {
      spawnTile(FX + (storm ? "water_storm" : "water"), 32, -224 + <f32>c * 64, -352 + <f32>r * 64, -384, 320);
    }
  }
  if (!storm) {
    spawnIsland(rand(-200, 0));
    spawnIsland(rand(120, 300));
  }
}
spawnCloud(rand(-200, 300));

export function onUpdate(dt: f32): void {
  decorTimer -= dt;
  if (decorTimer <= 0 && !storm) {
    decorTimer = land ? rand(0.45, 0.9) : rand(4.0, 7.5);
    if (land) spawnLandProp(380);
    else spawnIsland(400);
  }
  cloudTimer -= dt;
  if (cloudTimer <= 0) {
    cloudTimer = storm ? rand(0.8, 1.8) : rand(2.0, 4.0);
    spawnCloud(rand(380, 440));
  }
}
