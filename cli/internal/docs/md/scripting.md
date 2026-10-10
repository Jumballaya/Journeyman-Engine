# Scripting API (`@jm/runtime`)

Game logic is written in [AssemblyScript](https://www.assemblyscript.org/), a
TypeScript subset that compiles to WebAssembly. Each entity with a
`ScriptComponent` runs its own copy of a script, so module-level variables
are that entity's state. That copy is a whole WebAssembly instance with its
own memory, and creating one runs the script's top-level code: about 0.5 ms
for a Strike Wing enemy, close to a whole frame's work. Keep scripts off
things spawned by the dozen every second, like bullets and particles. Give those a `VelocityComponent`
and a `LifetimeComponent`, and let one script (the gun, a spawner) drive them.

A script looks like this:

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
- **`onMessage(message)`** runs for each message sent to this entity (see
  *Messages and shared data*), before its next `onUpdate`.
- Every hook is optional.

`jm build` compiles every script listed in `.jm.json` and extracts the runtime
into `assets/scripts/node_modules/@jm/runtime/`, so editors get completions.
Shared code can live in other `.ts` files that scripts import (the demos use
`assets/scripts/lib/`), or in a library shared between projects (see
*Building scripts*).

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
me.velocity.setAcceleration(0, -900);  // e.g. gravity, added to the velocity every second
me.sprite.play("walk");           // switch animation; keeps running if already playing
me.sprite.restart("explode");     // play from the first frame even if running
me.sprite.animation;              // the one playing ("" if none)
me.sprite.finished;               // a non-looping animation reached its end
me.sprite.setTexture("assets/atlases/ui.atlas.json#open");  // from the next frame; stops animating
me.text.set("120");               // TextComponent: text in the world (damage numbers)
me.text.setColor(1, 0.8, 0.2);  me.text.alpha = 0.5;  me.text.size = 8;
me.collider.layerMask = 2;        // also halfWidth, halfHeight, offsetX/Y, collidesWithMask
me.collider.solid = true;         // blocks every layer's move(); or me.collider.blocksMask = 1
const hit = me.move(dx, dy, 6);   // stops flush at solid colliders and drawn ground (x, then y), exactly;
                                  // slides 6 units into gaps (among boxes, not near drawn ground)
hit.onGround;  hit.hitX;  hit.hitY;  hit.byX;  hit.byY;  // sides blocked (-1/+1), and by what
hit.normalX;  hit.normalY;        // the surface met along y, facing it (standing: the ground's, leaning on slopes)
me.walk(dx, dy);                  // platformers: like move, but walks up slopes to 50° and 1-unit ledges,
                                  // down slopes and steps without leaving them (unless rising)
me.walk(dx, dy, true);            // dropThrough: fall through one-way platforms
lift.move(0, 2);                  // a solid mover (or terrain) carries what stands on it (solid boxes: if they
                                  // have a VelocityComponent); a ceiling over a rider stops the lift too;
                                  // a solid box pushes bodies with a VelocityComponent it runs into (not
                                  // ones solid to it: those stop it); pinned against a wall, a body
                                  // stays in it (crushed: onCollide reports the overlap); a rider goes
                                  // across with one platform a frame (the first to carry it)
me.circle.radius = 12;            // CircleColliderComponent: also offsetX/Y, layerMask, collidesWithMask
                                  // (move() goes by the box: give a mover a BoxColliderComponent)
me.lifetime.seconds = 1;          // destroyed when it runs out

other.isAlive;                    // false once destroyed
other.hasTag("enemy");  other.addTag("stunned");  other.removeTag("stunned");
other.has("VelocityComponent");
other.destroy();                  // removed at the end of the frame
other.equals(me);
```

`Physics` asks where colliders (boxes and circles) and drawn ground are,
without moving anything: line of sight, ground probes, what an attack's
reach covers. A ray passes ground it starts on, and a one-way platform stops
only rays heading down onto it. Each
query can skip one entity (the caster) and look only at some layers (a mask
of your own, e.g. `const ENEMIES: u32 = 4`). Lists come boxes, then circles,
then ground.

```ts
import { Physics } from "@jm/runtime";

const hit = Physics.raycast(x, y, 1, 0, 200, me, ENEMIES);  // rightward 200 units, skipping me
if (hit) { hit.entity; hit.x; hit.y; hit.normalX; hit.normalY; hit.distance; }
Physics.overlapCircle(x, y, 24, me, ENEMIES);  // Entity[]
Physics.overlapBox(x, y, 16, 8);               // half width and height, from the center
Physics.at(pointerX, pointerY);                // the colliders under a point (ground has no area)
```

A ray's direction needs no particular length (finite), its distance may be
`Infinity`, and one starting inside a collider hits it at 0 (hence `ignore`).

Entities nest (scenes and prefabs author it with `children`, see
[content.md](content.md#children)). A child moves and turns with its parent
and is destroyed with it:

```ts
me.parent;                        // Entity.NONE at the top level
me.children;                      // Entity[] in authored order
me.child("Sword");                // by name, or Entity.NONE
const sword = spawn("sword", me.transform.x, me.transform.y);
sword.attach(me);                 // keeps its place; from the end of this frame
sword.local.setPosition(13, 0);   // its place relative to the parent (also z, rotation)
sword.detach();                   // stays where it is
```

A child's `transform` is where it is in the world (each frame it's set from
the parent and `local`), so move children through `local`.

Component properties read and write the live component: there is nothing to
load or save. On an entity without that component, reads return 0 and writes
do nothing. Entity handles stay safe to keep across frames.

Writes to an entity spawned this frame (and `sprite.play`, `text.set`) are
kept and applied as soon as it exists, so a script can spawn and set up an
entity in the same breath. Reads see its values from the next frame.

Your own C++ components can expose fields too (see the README's *Extending
the engine*); reach them with a `Field`:

```ts
const HP = new Field("HealthComponent", "hp");
HP.set(me, HP.get(me) - 1);
```

## Moving bodies

One thing moves each body; pick one per entity:

- **Free:** a `VelocityComponent` (the default `"motion": "free"`). Physics moves it
  through everything: bullets, particles, backdrops.
- **Velocity, blocked:** `"motion": "move"` (top-down, exact) or `"walk"`
  (platformers: slopes, ledges, one-way platforms), with a `BoxColliderComponent`.
  Physics moves it as `move()`/`walk()` would and zeroes its velocity on a blocked
  side, so landing stops a fall. Set its velocity; don't also call `move()`/`walk()`.
- **By hand:** call `me.move()` or `me.walk()` each frame (no velocity, or a zero
  one: a solid body needs a `VelocityComponent` to ride lifts).

```ts
// Scene: "BoxColliderComponent": { "halfExtents": [6, 12] },
//        "VelocityComponent": { "acceleration": [0, -900], "motion": "walk" }
export function onUpdate(dt: f32): void {
  const v = me.velocity;
  v.x = Input.axis("left", "right") * 120;
  if (v.onGround && Input.pressed("jump")) v.y = 320;
  v.dropThrough = Input.down("down");      // through one-way platforms while held
}
```

`me.velocity.motion = "move"` switches it in a script; `blockedX`, `blockedY` are
the last step's blocked sides (-1/+1), `support` what it stands on (`Entity.NONE` in
the air) and `supportVelocityX/Y` how fast that goes if a velocity moves it (a lift
moved with `move()` reads 0): a jump off a rising lift is `v.y = 320 + v.supportVelocityY`. A lift or moving platform driven by
velocity is `"motion": "move"` with a solid box (a free one carries nobody);
being blocked stops its velocity, so set it again each frame or when it turns.

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
`.texture(image)`, `.acceleration(x, y)`, `.lifetime(s)`, `.tag(name)`
(so `World.find` can name it) and `.json(component, property, rawJson)` for
anything else. Scene entities are tagged with their `name`.

## Messages and shared data

Scripts don't share variables; they talk through entities:

```ts
door.send("open");                       // name, optional text and number
enemy.send("hit", "", 2);
World.broadcast("enemy", "freeze");      // every live entity with the tag

export function onMessage(message: Message): void {
  if (message.name == "hit") hp -= <i32>message.number;
  // message.from (an Entity), message.text
}

door.params.text("key");                 // another entity's script params
hero.data.setNumber("room", 3);          // values on an entity any script can read
World.find("hero").data.getNumber("room");
```

Messages arrive before the receiver's next update (a paused script gets them
when it runs again); an entity without a script ignores them. In a
multiplayer session, `send` reaches a shared entity on the machine that
simulates it, and `entity.broadcast` reaches every copy; `message.player`
says who sent it. The `Net` API (sessions, players, messages between
machines) is in [networking.md](networking.md#scripting-net). `entity.data`
is a `Store` like `GameState` (below) that belongs to the entity and goes
away with it.

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
Input.repeated("left", 0.16, 0.05);  // on press, then every 0.05s once held 0.16s
Input.bind("fire", "Gamepad.RightBumper");   Input.unbind("fire");
Input.gamepadConnected;
Input.keyPressed(Key.F11);           // raw keys: keyDown, keyPressed, keyReleased (import { Key })
```

The mouse: buttons are keys (`Key.MouseLeft`, `MouseRight`, `MouseMiddle`,
or bind `"MouseLeft"` to an action); the pointer is in screen (UI) pixels,
the same space `Camera.toWorld` takes, so finding what's under it is two calls:

```ts
const at = new Vec2(), world = new Vec2();
if (Input.pointerInside && Input.keyPressed(Key.MouseLeft)) {
  Camera.toWorld(Input.pointer(at).x, at.y, world);   // then map.tileX(world.x)...
}
Input.wheel;                         // the last frame's scroll, up is positive (also wheelX)
```

In the editor the Game view passes the mouse to the running game while the
pointer is over it.

Modifier keys are `LeftShift`/`RightShift`, `LeftCtrl`/`RightCtrl`,
`LeftAlt`/`RightAlt` and `LeftSuper`/`RightSuper`; binding `"Shift"` (or
`"Ctrl"`, `"Alt"`, `"Super"`) binds both sides.

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
UI.worldRect("well");                // the element's box in world units (Rect), or null
```

`UI.worldRect` reports the last drawn layout (null before the first frame is
drawn), so sprites can line up with the HTML instead of copying its sizes.

A `.hidden { display: none !important; }` class toggled with
`UI.toggleClass(id, "hidden", !visible)` keeps visibility in CSS.

## Scenes and transitions

```ts
Scene.load("level2");                    // swap at the end of the frame
Scene.transition("level2");              // 0.5s crossfade
Scene.transition("level2", 1.0, "wipe"); // with a transition shader
Scene.transitioning;                     // a transition is running
Scene.current;                           // "scenes/level1.scene.json"
Scene.spawnGroup("room-1-0");            // the scene's entries in that group
Scene.despawnGroup("room-1-0");          // destroys the members still alive
Scene.groupSpawned("room-1-0");
```

Groups and the `if`/`unless` conditions on scene entries are described in
[content.md](content.md#groups-and-conditions).

A request made while a transition runs waits for it to finish; the latest
request wins. Loading a scene destroys every entity of the previous one,
removes its post-effects and resets the time scale to 1.

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
Camera.setPosition(0, 100);        // the view's center; Camera.x, Camera.y
Camera.zoom = 2;                   // 2: twice as close; Camera.width, Camera.height: the world it shows
Camera.toWorld(sx, sy, out);       // screen (UI) pixels <-> world units
Camera.toScreen(x, y, out);
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
GameState.setStrings("flags", ["chest.1"]);  GameState.getStrings("flags");  // also Numbers
GameState.setJson("party", value);  GameState.getJson("party");   // any JsonValue
GameState.keys("item.");           // keys starting with a prefix, sorted

Save.setNumber("hiscore", 98765);  Save.getNumber("musicVolume", 0.7);
```

`GameState` is shared by all scripts and survives scene changes (score,
lives, current stage). `Save` has the same API but is written to `save.json`
in the player's data directory at the end of any frame that changed it. Use
it for high scores and settings. Wrapping your keys in a typed class keeps
them in one place (and *Shared sessions* in `runtime-gameplay` does it for you).

## Data files

Content can live in JSON or text files listed in the manifest:

```ts
const bestiary = Data.json("bestiary");       // assets/data/bestiary.json
bestiary.get("enemies").at(0).get("hp").int();
bestiary.get("boss").strings();               // also number(), text(), bool(), keys(), length
Data.text("assets/data/intro.txt");
Json.parse(text);  value.toString();          // JsonValue builds with set/push too
```

Missing files and fields read as null values, which fall back (`number(7)`).

## Tile maps

A `TileMapComponent` draws a Tiled map (format in
[content.md](content.md#tile-maps)); scripts ask it what is where. Tiles are
known by their type, set in the tileset:

```ts
const map = TileMap.find("map");
map.at(tx, ty);                        // the topmost tile's type ("" if none); (0, 0) is the bottom-left
map.set(tx, ty, "door_open");          // on the topmost layer with a tile there; "" clears it
map.set(tx, ty, "brick", "collision"); // on a layer by name
map.solid(tx, ty);  map.is(tx, ty, "deadly");  map.solidAt(x, y);
map.tileX(x);  map.centerX(tx);        // world <-> tile; also tileY, centerY, tileWidth, tileHeight
map.positionsOf("stairs");             // [tx, ty, ...] of every tile of a type
map.objects("exit");                   // MapObjects of a type (all with no argument): x, y, width, height, name, type, properties,
                                       // and a polyline's or polygon's points ([x, y, ...], world units) and closed
map.object("start");                   // one by name, or null
map.properties.get("music").text();    // the map's custom properties
map.showLayer("roofs", false);         // hide a layer (tiles, images, objects)
map.load("assets/maps/cave.tmj");      // another map in its place

const body = new TileBody(5, 5);       // a box (half size) at body.x, body.y
body.move(map, dx, dy, 6);             // stops flush at solid tiles; slides 6 units into openings
body.onGround;  body.hitX;  body.hitY; // what it ran into, and body.hitTileX/hitTileY
```

## App & logging

```ts
App.quit();
log("hp=", hp.toString());         // stdout and logs/engine.log
```

## Building scripts

`jm build` compiles each script with AssemblyScript, run by Node. Neither
needs installing:

- **Node**: the machine's own when it is 20 or newer, else a pinned Node LTS
  that jm downloads once. `JM_TOOLCHAIN=managed` always uses the pinned one.
- **AssemblyScript**: the project's own when `assets/scripts/node_modules` has
  it (`npm install` there, e.g. for editor completions on `tsconfig.json`),
  else a pinned one jm downloads once.

Downloads are checked against checksums built into jm and kept in
`~/.jm/toolchains` (or `JM_TOOLCHAIN_DIR`). `jm doctor` says which Node and
compiler a build will use; `jm doctor --fetch` downloads them now, for a CI
cache or a container image.

Code shared between projects lives in a folder named in the manifest's
`scriptLibraries` (`{"@demos/common": "../common"}`): `jm build` and `jm test`
copy it into `node_modules`, so scripts `import { Dialog } from "@demos/common"`.

`jm test` compiles `tests/*.spec.ts` and runs every exported function as a
test (a failed `assert` fails it), with an in-memory `GameState`/`Save` and the
project's data files. No `jm build` is needed first. See [testing.md](testing.md).

A script that traps (a failed assertion, an out-of-bounds access) is logged
with its script path and entity and stops running; the rest of the game keeps
going. Failed assertions also log their message and source line.

A script can't hang the game, either: each call into it (its top-level code,
`onUpdate`, `onCollide`, a message) may take up to 25 million steps (function
calls and loop iterations; the demos' busiest call takes about 420,000). One
that runs past that, an endless loop say, traps like any other error: "ran
out of fuel". Steps, not time, so it stops at the same point on every machine.
