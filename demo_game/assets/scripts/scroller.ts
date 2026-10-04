// Endless parallax background. Param "theme": ocean | storm | land.
// A grid of ScrollWrap tiles forms the ground; islands, land props and
// clouds are spawned above the screen and drift down past it.
import { World, Params, Renderer } from "@jm/runtime";
import { HALF_W, rand, randInt } from "./lib/game";

const FX = "assets/atlases/fx.atlas.json#";
const SH = "assets/atlases/shmup.atlas.json#";

const LAND_PROPS: string[] = [
  "tile_0048", "tile_0048", "tile_0060", "tile_0060", "tile_0036", "tile_0072", "tile_0084",
  "tile_0078", "tile_0090", "tile_0096", "tile_0102", "tile_0116", "tile_0116", "tile_0056",
  "tile_0054", "tile_0066", "tile_0109", "tile_0111", "tile_0117",
];
const ISLANDS: string[] = ["island_0", "island_1", "island_2", "island_3"];
const ISLAND_W: f32[] = [56, 40, 72, 30];
const ISLAND_H: f32[] = [44, 32, 52, 26];

let theme = "ocean";
let speed: f32 = 40;
let started = false;
let decorTimer: f32 = 0;
let cloudTimer: f32 = 0;

// Land is dimmed so planes, shots and the HUD read clearly against it.
function groundTint(): string {
  return theme == "land" ? "[0.62,0.7,0.6,1]" : "[1,1,1,1]";
}

function spawnTile(texture: string, scale: f32, x: f32, y: f32, minY: f32, maxY: f32): void {
  World.spawn("assets/prefabs/bg_tile.prefab.json", x, y,
    '{"SpriteComponent":{"texture":"' + texture + '","color":' + groundTint() + "}," +
    '"TransformComponent":{"scale":[' + scale.toString() + "," + scale.toString() + "]}," +
    '"VelocityComponent":{"velocity":[0,' + (-speed).toString() + "]}," +
    '"ScrollWrapComponent":{"minY":' + minY.toString() + ',"maxY":' + maxY.toString() + "}}");
}

function spawnDecor(texture: string, sx: f32, sy: f32, x: f32, y: f32): void {
  World.spawn("assets/prefabs/bg_decor.prefab.json", x, y,
    '{"SpriteComponent":{"texture":"' + texture + '","color":' + groundTint() + "}," +
    '"TransformComponent":{"scale":[' + sx.toString() + "," + sy.toString() + "]}," +
    '"VelocityComponent":{"velocity":[0,' + (-speed).toString() + "]}}");
}

function spawnIsland(y: f32): void {
  const i = randInt(0, ISLANDS.length - 1);
  spawnDecor(FX + ISLANDS[i], ISLAND_W[i], ISLAND_H[i], rand(-HALF_W + 30, HALF_W - 30), y);
}

function spawnLandProp(y: f32): void {
  const col = randInt(0, 14);
  spawnDecor(SH + LAND_PROPS[randInt(0, LAND_PROPS.length - 1)], 16, 16, -224 + <f32>col * 32, y);
}

function spawnCloud(y: f32): void {
  const k = randInt(0, 2);
  const w: f32 = k == 0 ? 64 : k == 1 ? 48 : 80;
  const h: f32 = k == 0 ? 32 : k == 1 ? 24 : 36;
  const scale = rand(1.0, 1.6);
  const tint = theme == "storm" ? "[0.55,0.6,0.7,0.7]" : "[1,1,1,0.5]";
  World.spawn("assets/prefabs/cloud.prefab.json", rand(-HALF_W, HALF_W), y,
    '{"SpriteComponent":{"texture":"' + FX + "cloud_" + k.toString() + '","color":' + tint + "}," +
    '"TransformComponent":{"scale":[' + (w * scale).toString() + "," + (h * scale).toString() + "]}," +
    '"VelocityComponent":{"velocity":[0,' + (-speed * rand(2.4, 3.2)).toString() + "]}}");
}

function start(): void {
  started = true;
  theme = Params.string("theme", "ocean");
  speed = <f32>Params.number("speed", theme == "land" ? 60 : 40);

  if (theme == "land") {
    Renderer.setClearColor(0.443, 0.585, 0.141);
    // 32px grass grid, 15 x 22 tiles, wrapping every 704px.
    for (let r = 0; r < 22; r++) {
      for (let c = 0; c < 15; c++) spawnTile(SH + "tile_0110", 16, -224 + <f32>c * 32, -336 + <f32>r * 32, -368, 336);
    }
    for (let y: f32 = -320; y < 360; y += 48) spawnLandProp(y);
  } else {
    const storm = theme == "storm";
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
}

export function onUpdate(dt: f32): void {
  if (!started) start();
  decorTimer -= dt;
  if (decorTimer <= 0) {
    if (theme == "land") {
      decorTimer = rand(0.45, 0.9);
      spawnLandProp(380);
    } else if (theme == "ocean") {
      decorTimer = rand(4.0, 7.5);
      spawnIsland(400);
    } else {
      decorTimer = 100;
    }
  }
  cloudTimer -= dt;
  if (cloudTimer <= 0) {
    cloudTimer = theme == "storm" ? rand(0.8, 1.8) : rand(2.0, 4.0);
    spawnCloud(rand(380, 440));
  }
}
