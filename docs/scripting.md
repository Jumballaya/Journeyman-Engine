# Scripting API (`@jm/runtime`)

Game logic is written in [AssemblyScript](https://www.assemblyscript.org/), a
TypeScript subset that compiles to WebAssembly. Each entity with a
`ScriptComponent` runs its own copy of a script, so module-level variables
are that entity's state:

```ts
import { Entity, Input, Timer, self, spawn } from "@jm/runtime";

const me = self();            // the entity this script runs on
const cooldown = new Timer();

export function onUpdate(dt: f32): void {
  me.transform.x += Input.axis("left", "right") * 200 * dt;
  cooldown.tick(dt);
  if (Input.down("fire") && cooldown.ready) {
    cooldown.start(0.2);
    spawn("bullet", me.transform.x, me.transform.y);
  }
}

export function onCollide(other: Entity): void {
  if (other.hasTag("enemy")) me.destroy();
}
```

- **Top-level code** runs once, when the entity starts (its first frame, after
  all of its components exist). Use it for setup.
- **`onUpdate(dt)`** runs every frame; `dt` is in seconds (see *Time & pause*).
- **`onCollide(other)`** runs when this entity's collider touches another one.
- Both hooks are optional.

`jm build` compiles every script listed in `.jm.json` and extracts the runtime
into `assets/scripts/node_modules/@jm/runtime/`, so editors get completions.
Shared code can live in other `.ts` files that scripts import (the demo uses
`assets/scripts/lib/`).

AssemblyScript notes: number types are explicit (`f32`, `i32`, `f64`); use
`Mathf` for `f32` math; closures can't capture local variables; `Math.random()`
works.

For menus, projectiles, timers, timelines, math, HUDs and checkpoints, see
[Gameplay building blocks](runtime-gameplay.md). The demo uses these directly;
its shared scripts retain only Strike Wing's rules and presentation choices.

## Names instead of paths

Prefabs, scenes, shaders and sounds can be named by their file name: `"bullet"`
finds `assets/prefabs/bullet.prefab.json`, `"level2"` finds
`scenes/level2.scene.json`, `"crt"` finds `assets/shaders/crt.frag`, and
`"laser"` finds `assets/sounds/laser.wav`, as long as the file is listed in
`.jm.json`. Full paths work everywhere too.

## Entities and components

```ts
const me = self();
me.transform.x = 10;              // position (world units, y up), z = draw order
me.transform.setPosition(0, -200);
me.transform.rotation = 0.5;      // radians
me.transform.setScale(16, 16);    // half size: sprite quads span -1..1
me.velocity.set(0, 300);          // units per second, moved by physics
me.sprite.setColor(1, 0.5, 0.5);  // tint; me.sprite.alpha = 0.5
me.sprite.play("explode");        // SpriteAnimationComponent animation
me.sprite.finished;               // a non-looping animation reached its end
me.collider.layerMask = 2;        // also halfWidth, halfHeight, offsetX/Y, collidesWithMask
me.lifetime.seconds = 1;          // destroyed when it runs out

other.isAlive;                    // false once destroyed
other.hasTag("enemy");  other.addTag("stunned");  other.removeTag("stunned");
other.has("VelocityComponent");
other.destroy();                  // removed at the end of the frame
other.equals(me);
```

Component properties read and write the live component: there is nothing to
load or save. On an entity without that component, reads return 0 and writes
do nothing. Entity handles stay safe to keep across frames.

Your own C++ components can expose fields too (see the README's *Extending
the engine*); reach them with a `Field`:

```ts
const HP = new Field("HealthComponent", "hp");
HP.set(me, HP.get(me) - 1);
```

## Spawning and finding

```ts
const b = spawn("bullet", x, y);                      // a prefab at (x, y)
spawn("enemy", x, 360, new Overrides()
  .velocity(0, -120)
  .rotation(Mathf.PI)
  .tint(1, 0.5, 0.5)
  .param("hp", 3)                                     // ScriptComponent params
  .paramText("pattern", "dive"));

World.find("player");      // any entity with the tag, or Entity.NONE
World.findAll("enemy");    // Entity[]
World.count("enemy_bullet");
```

The new entity appears at the end of the frame (its handle is valid right
away; component writes to it take effect once it exists). Spawned entities
belong to the current scene. `Overrides` also has `.scale(x, y)`,
`.texture(image)`, `.lifetime(s)` and `.json(component, property, rawJson)`
for anything else. Scene entities are tagged with their `name`.

## Script parameters

One script can drive many variants through `params`:

```json
"ScriptComponent": { "script": "assets/scripts/enemy.ts", "params": { "hp": 3, "pattern": "swoop" } }
```
```ts
const hp = Params.number("hp", 1);          // fallback if missing
const pattern = Params.text("pattern", "straight");
```

## Input

Prefer named actions (from a `.bindings.json` asset, see
[content.md](content.md#input-bindings)); they work on keyboard and gamepad.

```ts
Input.down("fire");                  // held
Input.pressed("pause");              // went down this frame
Input.released("fire");              // went up this frame
Input.value("right");                // 0..1, analog for sticks and triggers
Input.axis("left", "right");         // -1..1
Input.bind("fire", "Gamepad.RightBumper");   Input.unbind("fire");
Input.gamepadConnected;
Input.keyPressed(Key.F11);           // raw keys: keyDown, keyPressed, keyReleased
```

## Audio

```ts
const laser = new Sound("laser");    // reuse one Sound per effect
laser.play(0.5);                     // gain
const theme = new Music("theme");    // loops on the Music bus
theme.play();
theme.fadeOut(1.5);  theme.stop();  theme.gain = 0.3;

Audio.setVolume(Bus.Music, 0.6);     // Master, Music, Sfx
Audio.stopAll(0.5);                  // fade everything out
```

Everything fades out quickly when a scene unloads.

## UI

Screens are `.ui.html` documents shown by entities with a
`UIDocumentComponent` (see [content.md](content.md#ui-htmlcss)). Scripts change
elements by `id`, across every document on screen:

```ts
UI.setText("score", "0012340");
UI.addClass("item-2", "selected");   UI.removeClass(id, cls);   UI.toggleClass(id, cls, on);
UI.setStyle("boss-fill", "width", "40%");   // "" removes the inline property
UI.setAttribute("portrait", "src", "assets/atlases/ui.atlas.json#face2");
UI.hide("panel");  UI.show("panel");  UI.setVisible("panel", on);
UI.exists("panel");
```

A `.hidden { display: none !important; }` class toggled with
`UI.toggleClass(id, "hidden", !visible)` keeps visibility in CSS.

## Scenes and transitions

```ts
Scene.load("level2");                    // swap at the end of the frame
Scene.transition("level2");              // 0.5s crossfade
Scene.transition("level2", 1.0, "wipe"); // with a transition shader
Scene.transitioning;                     // a transition is running
Scene.current;                           // "scenes/level1.scene.json"
```

A transition request is ignored while another one runs. Loading a scene
destroys every entity of the previous one, removes its post-effects and
resets the time scale to 1.

## Rendering

```ts
const crt = PostEffect.custom("crt").setFloat("u_strength", 1.0);
crt.setVec3("u_tint", 1, 0, 0);    // also setVec2, setVec4
crt.enabled = false;
crt.remove();

PostEffect.builtin("vignette").setFloat("u_strength", 0.6);
// grayscale, blur (u_radius), pixelate (u_pixelSize), colorshift (u_hsvDelta),
// vignette (u_strength), flash (u_color, u_amount)

Camera.shake(8, 0.4);              // amplitude (world units), seconds
Camera.setPosition(0, 100);
Renderer.setClearColor(0.1, 0.2, 0.4);
Window.fullscreen = true;
Window.focused;                    // false while another app has focus
```

Post-effects run in the order added and belong to the current scene.

## Time & pause

```ts
Time.pause();  Time.resume();  Time.paused;
Time.scale = 0.5;                  // slow motion
Time.elapsed;  Time.unscaledElapsed;  Time.unscaledDelta;
```

While paused, gameplay scripts don't run and physics, animation and
collisions freeze. Scripts whose `ScriptComponent` sets
`"runWhenPaused": true` keep running with unscaled `dt`: use that for pause
menus.

## Game state & saves

```ts
GameState.setNumber("score", 0);   GameState.add("score", 100);
GameState.getNumber("lives", 3);   GameState.setString("difficulty", "hard");
GameState.has("score");  GameState.remove("score");  GameState.clear();

Save.setNumber("hiscore", 98765);  Save.getNumber("musicVolume", 0.7);
```

`GameState` is shared by all scripts and survives scene changes (score,
lives, current stage). `Save` has the same API but is written to `save.json`
in the player's data directory at the end of any frame that changed it. Use
it for high scores and settings. Wrapping your keys in a typed class keeps
them in one place (see the demo's `lib/session.ts`).

## App & logging

```ts
App.quit();
log("hp=", hp.toString());         // stdout and logs/engine.log
```

## Building scripts

`jm build` compiles each script with `npx asc` from `assets/scripts/`. First
time per project: `cd assets/scripts && npm install`. If `asc` reports missing
modules or version mismatches, reinstall: `rm -rf node_modules && npm install`.
In CI, use `npm ci` then `jm build`.

A script that traps (a failed assertion, an out-of-bounds access) is logged
with its entity and stops running; the rest of the game keeps going.
