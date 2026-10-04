# Scripting API (`@jm/runtime`)

Game logic is written in [AssemblyScript](https://www.assemblyscript.org/)
(a TypeScript subset that compiles to WebAssembly). Each entity with a
`ScriptComponent` runs its own instance of a script, so module-level variables
are per-entity state.

```ts
import { Input, TransformComponent } from "@jm/runtime";

const tr = new TransformComponent();

export function onUpdate(dt: f32): void {
  if (!tr.read()) return;               // this entity's transform
  tr.x += Input.axis("left", "right") * 200 * dt;
  tr.write();
}

// Optional: called when this entity's collider overlaps another one.
export function onCollide(index: u32, generation: u32): void {}
```

Every script must export `onUpdate(dt: f32)`. `dt` is in seconds, scaled by
`Time.scale` (see *Time & pause*). The embedded runtime is extracted into
`assets/scripts/node_modules/@jm/runtime/` on every `jm build`, so editors get
completions from it. Shared code can live in other `.ts` files that scripts
import (e.g. `assets/scripts/lib/`); only scripts used by a `ScriptComponent`
need to be listed in `.jm.json`.

AssemblyScript notes: number types are explicit (`f32`, `i32`, `f64`…); use
`Mathf` for `f32` math; closures can't capture locals; `Math.random()` works
(the engine seeds it).

## Entities and the world

```ts
import { Entity, World } from "@jm/runtime";

const me = Entity.self();
const bullet = World.spawn("assets/prefabs/bullet.prefab.json", x, y,
  '{"VelocityComponent":{"velocity":[0,400]}}');   // optional prefab overrides
const player = World.find("player");              // any live entity with the tag, or Entity.NONE
const enemies = World.findAll("enemy");
const count = World.count("enemy_bullet");

other.isAlive;          // false once destroyed (or scheduled for destruction)
other.hasTag("enemy");
other.addTag("stunned"); other.removeTag("stunned");
bullet.destroy();       // deferred to the end of the frame
```

- `World.spawn` returns the new entity's handle immediately; the prefab is
  instantiated at the end of the frame. The position overrides the prefab's
  transform (its z/draw order is kept). Overrides use the same shape as scene
  prefab overrides and merge recursively into the prefab's components.
- Spawned entities belong to the current scene and are destroyed with it.
- Destroyed entities stop colliding immediately, so a bullet that hits two
  enemies in one frame only counts once.
- Entity handles are `(index, generation)` pairs: safe to keep across frames.

## Components

Each component class is a script-side copy you `read()` and `write()`.
Both default to the script's own entity; pass an `Entity` for any other.

```ts
const t = new TransformComponent();
if (t.read(World.find("player"))) aimAt(t.x, t.y);
```

| Class | Fields |
|---|---|
| `TransformComponent` | `x y z` (z = draw order), `sx sy` (half size: sprite quads span −1..1), `rotation` (radians) |
| `SpriteComponent` | `r g b a` tint, `tx ty tu tv` texture rect, `layer`; `setColor(r,g,b,a)` |
| `VelocityComponent` | `vx vy` (units/second, integrated by the physics module) |
| `BoxColliderComponent` | `halfWidth halfHeight offsetX offsetY layerMask collidesWithMask` |
| `LifetimeComponent` | `seconds` remaining before auto-destroy |

## Script parameters

Per-instance values authored on the `ScriptComponent` (and overridable per
spawn) let one script drive many variants:

```json
"ScriptComponent": { "script": "assets/scripts/enemy.ts", "params": { "hp": 3, "pattern": "swoop" } }
```
```ts
const hp = Params.number("hp", 1);
const pattern = Params.string("pattern", "straight");
World.spawn("assets/prefabs/enemy.prefab.json", x, y, '{"ScriptComponent":{"params":{"pattern":"dive"}}}');
```

## Input

Prefer named actions (defined in a `.bindings.json` asset, see
[content.md](content.md#input-bindings)); they work on keyboard and gamepad.

```ts
Input.down("fire");                 // held
Input.pressed("pause");             // went down this frame
Input.released("fire");
Input.value("right");               // 0..1, analog for sticks/triggers
Input.axis("left", "right");        // -1..1
Input.bind("fire", "Gamepad.RightBumper");
Input.unbind("fire");
Input.gamepadConnected;

Inputs.keyIsDown(Key.Space);        // raw keys are still available
```

## Audio

```ts
const laser = new Sound("assets/sounds/laser.wav");          // or just "laser.wav"
laser.play(0.5);                                             // gain
const music = new Sound("assets/sounds/theme.wav", Bus.Music);
music.play(1.0, true);                                       // looping
music.fadeOut(1.5);  music.stop();  music.gain = 0.3;

Audio.setVolume(Bus.Music, 0.6);    // Master, Music, Sfx
Audio.stopAll(0.5);                 // fade everything
```

All sounds fade out quickly when a scene unloads.

## UI

Screens are `.ui.html` documents attached to entities with a
`UIDocumentComponent` (see [content.md](content.md#ui-htmlcss)). Scripts drive
elements by `id` across every live document:

```ts
UI.setText("score", "0012340");
UI.addClass("item-2", "selected");   UI.removeClass(...);  UI.toggleClass(id, cls, on);
UI.setStyle("boss-fill", "width", "40%");   // "" removes the inline property
UI.setAttribute("portrait", "src", "assets/atlases/ui.atlas.json#face2");
UI.hide("panel"); UI.show("panel");          // inline display:none on/off
UI.exists("panel");
```

A common pattern is a `.hidden { display: none !important; }` utility class
toggled with `UI.toggleClass(id, "hidden", !visible)`.

## Scenes and transitions

```ts
Scene.load("scenes/level2.scene.json");                    // immediate swap
Scene.transition("scenes/level2.scene.json", 0.8);         // crossfade
Scene.transition("scenes/level2.scene.json", 1.0, "assets/shaders/wipe.frag");
Scene.isTransitioning();
Scene.current();                                           // "scenes/level1.scene.json"
```

Requests apply at the end of the frame. A transition is ignored while another
is running. Post-effects added by a scene's scripts are removed when it
unloads, and the time scale resets to 1 for every new scene.

## Rendering: post-effects, camera, clear color, window

```ts
const crt = PostEffect.custom("assets/shaders/crt.frag");  // shader must be in the manifest
crt.setUniform("u_strength", 1.0);
crt.setUniformVec3("u_color", 1, 0, 0);   crt.setUniformVec4(...);
crt.setEnabled(false);  crt.remove();

new PostEffect(BuiltinEffect.Vignette).setUniform("u_strength", 0.6);
// Builtins: Passthrough, Grayscale, Blur (u_radius), Pixelate (u_pixelSize),
// ColorShift (u_hsvDelta vec3), Crossfade, Vignette (u_strength), Flash (u_color, u_amount)

Camera.shake(8, 0.4);          // amplitude (world units), duration (s)
Camera.setPosition(0, 100);
Renderer.setClearColor(0.1, 0.2, 0.4);
Window.fullscreen = true;

Sprite.setAnimation(e.index, e.generation, "explode");
Sprite.isAnimationFinished(e.index, e.generation);
```

## Time & pause

```ts
Time.pause();  Time.resume();  Time.paused;
Time.scale = 0.5;               // slow motion
Time.elapsed; Time.unscaledElapsed; Time.unscaledDelta;
```

While paused (scale 0), gameplay scripts don't run and physics, animation and
collisions freeze. Scripts whose `ScriptComponent` sets `"runWhenPaused": true`
keep running with unscaled `dt` — use that for pause menus.

## Game state & saves

```ts
GameState.setNumber("score", 0);   GameState.add("score", 100);
GameState.getNumber("lives", 3);   GameState.setString("difficulty", "hard");
GameState.has("score");  GameState.remove("score");  GameState.clear();

Save.setNumber("hiscore", 98765);  Save.getNumber("musicVolume", 0.7);
```

`GameState` is shared by all scripts and survives scene changes (score, lives,
current stage). `Save` has the same API but is written to `save.json` in the
player's data directory at the end of any frame that changed it — use it for
high scores and settings, not per-frame values.

## App & logging

```ts
App.quit();
Logger.log("hp=", hp.toString());   // stdout + logs/engine.log
```

## Building scripts

`jm build` compiles each script listed in `.jm.json` with `npx asc` from
`assets/scripts/`. First-time setup per project: `cd assets/scripts && npm
install`. If `asc` reports missing modules or version mismatches, wipe and
reinstall: `rm -rf assets/scripts/node_modules && npm install`. In CI, use
`npm ci` then `jm build`.

A script that traps (e.g. a failed assertion) is logged once with its entity
and disabled; the rest of the game keeps running.
