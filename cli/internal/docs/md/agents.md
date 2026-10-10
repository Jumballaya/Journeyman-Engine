# {{name}}: working on a Journeyman game

This is a game for the Journeyman 2D engine. All of it is files: edit them,
then use `jm` to build, test and play. Nothing needs the editor, and nothing
asks for input. Every command named here exists, and `jm <command> --help`
covers each one.

## Files

| Path | What it is |
|---|---|
| `.jm.json` | the manifest: name, entry scene, scene list, asset globs, window and renderer config |
| `scenes/*.scene.json` | scenes: entities, each with components (JSON) |
| `assets/prefabs/*.prefab.json` | entity templates that scripts `spawn("name", x, y)` |
| `assets/scripts/*.ts` | game logic in **AssemblyScript** (a TypeScript subset compiled to WebAssembly; it is not TypeScript) |
| `assets/ui/*.ui.html`, `*.css` | UI screens, in an HTML/CSS subset |
| `tests/*.spec.ts` | logic tests (`jm test`) |
| `tests/golden/` | reference frames (`jm golden`) |
| `build/` | made by `jm build` and wiped each time. Never edit it |

New files under `assets/` ship when they match a pattern in `.jm.json`'s
`assets` (a new project's `assets/**` matches all of them; an older one may
list folders or extensions). After `jm build`, `build/.jm.json` lists what
ships: check a new file is there. Scenes
are listed in `.jm.json`'s `scenes`: `jm generate scene level2` makes one and
adds it, and `entryScene` is the one the game starts in.
`assets/input.bindings.json` maps actions (`left`, `confirm`, ...) to keys
for `Input.down("left")`; edit it to add your own. `jm generate list` shows the
starting templates; for example `jm generate script player` and
`jm generate prefab bullet`.

## The loop

1. **Look things up:**
   - `jm schema` lists every component's keys, with defaults and choices;
     `jm schema SpriteComponent` shows one.
   - The script API is in `jm docs scripting`; its sources (one file per
     area: `entity.ts`, `input.ts`, `state.ts`, ...) are in
     `assets/scripts/node_modules/@jm/runtime/` after the first build.
2. **Edit** the files.
3. **Build:** `jm build --json`. Each line is a problem, given as
   `level`, `category`, `message`, `file`, `line` and `column`. The last line
   is `{"result": "ok"|"failed", ...}`. Fix every error; warnings about
   scene keys are almost always typos. The first build downloads the script
   compiler if the machine has none, and `jm doctor` shows what it uses.
4. **Test logic:** run `jm test` (`--json` for one JSON line per test). Each exported function in
   `tests/*.spec.ts` is a test, and `assert(cond, "message")` fails it.
   The game's state and its save live in memory there. Rendering and audio
   do nothing.
5. **Play it, headless and step by step** (over MCP: `drive_start`, `drive`,
   `drive_frame` to see it, `drive_stop`; never by clicking the game's
   window). The game only advances when told.
   Each line on stdin is a command, and each answer is one JSON line:
   ```sh
   printf 'step 60\npress Enter\nstep 120\nstate\nquit\n' | JM_DRIVE=1 JM_RENDERER=none jm run
   ```
   - `state` returns every entity with its components, the session state, the
     save, the UI layout and the frame's draw list: big. Ask for parts:
     `state session`, `state ui`, `state tag=Paddle` (entities with that tag),
     several at once (`state session tag=Ball tag=Paddle`), or one component
     (`state tag=Ball TransformComponent`). `get` answers a single value, handy
     in a shell loop: `get tag=Ball TransformComponent.x` gives
     `{"ok":true,"value":-52.4}`, `get session.score` too; `get camera.x` is
     where the camera looks.
   - Wait for something instead of guessing frame counts: `until tag=Lift
     TransformComponent.y < -270 max 600` steps until it's true (or says where
     it got after 600 frames). `echo <text>` labels a transcript.
   - Something should touch and doesn't (a pickup, a landing)? `near tag=Player`
     lists what's within 4 units, with the gaps: a 0.01 miss shows as `"gap": 0.01`.
   - Commands: `step [n]`, `state`, `get`, `until`, `echo`, `near`, `down|up|press <Key>`,
     `set <key> <json>`, `scene <path>`, `capture <file.png>` (needs GL: use
     `JM_HEADLESS=1` instead of `JM_RENDERER=none`), and `quit`.
   - Key names are the same in the driver, replay files and input bindings:
     `A`–`Z`, `Digit0`–`Digit9`, `Space`, `Enter`, `Escape`, `ArrowLeft`
     (the full list is under *Input bindings* in `jm docs content`). An
     unknown name is an error.
   - Errors the game logs come back in the next `step`'s `errors`. Add
     `JM_STRICT=1` to stop on the first one.
6. **Look at it**, if the machine has a display or software GL:
   ```sh
   JM_HEADLESS=1 JM_EXIT_AFTER_FRAMES=300 JM_CAPTURE_DIR=/tmp/f JM_CAPTURE_FRAMES=60,299 jm run
   ```
   Then view the PNGs. Through MCP, `drive_start` with `gl: true` and then
   `drive_frame` returns the driven game's current frame as an image: no
   files to place or clean up. On a Linux machine with no display, software GL is
   two packages away: `apt-get install -y xvfb libgl1-mesa-dri libglx-mesa0`,
   then prefix the command with `xvfb-run -a`. Once a screen looks right, record it with
   `jm golden --update` (`tests/golden/<name>.golden.json` lists the frames, an
   optional replay, and the scene to start in). From then on `jm golden`
   fails if it changes.
7. **Format:** run `jm fmt` before you commit. It writes the JSON layout the
   editor writes, so diffs stay small.

Keep the project to the game. Images you look at, logs and scratch files go
in `.jm/` (it ignores itself in git) or the temp dir, never the project root;
over MCP, `drive_frame` and `play_frame` return images with no file at all.
Reach for jm's own commands (the driver, plays, `jm test`, `jm golden`)
before writing a helper script: a wrapper around them is a second toolchain
the next agent has to learn.

No `jq` or `python`? `jm doctor --json` names the Node that jm uses for
scripts (`toolchain.node`), which can run a small JSON helper. Usually `get`
is enough.

Runs are deterministic: headless runs use a fixed 1/60 s step and seed 1. The
same inputs give the same frames and the same state, so a failure you found
once can be replayed.

If a tool speaks the Model Context Protocol, `jm mcp` serves these same
commands over stdio (`build`, `test`, `golden`, `fmt`, `export`, the driver,
the plays). `jm setup` adds it to the agent apps on this machine (Claude Code,
Claude Desktop, Codex); `jm setup chatgpt` prints the steps for ChatGPT.

## Platformers and moving things

- **Ground:** draw it in Tiled on an object layer as polylines or polygons of
  class `ground` (solid) or `platform` (one-way), or give a scene entity a
  `TerrainComponent` (`jm docs content`, *Drawn ground*).
- **Player:** a `BoxColliderComponent` plus `"VelocityComponent": {"acceleration":
  [0, -900], "motion": "walk"}`. Its script sets `me.velocity.x` and jumps
  when `me.velocity.onGround` (the example under *Moving bodies* in `jm docs scripting`).
  One thing moves a body: don't also call `move()`/`walk()` on it.
- **Lifts and carts:** `"BoxColliderComponent": {"halfExtents": [20, 2], "blocksMask": 1}`
  (solid: `blocksMask` defaults to 0, solid to nothing) plus `"VelocityComponent":
  {"motion": "move"}`. Whatever stands on it rides, and what it runs into is
  pushed. Being blocked stops its velocity, so its script sets it each frame.
- **Check it:** `get tag=Player VelocityComponent.blockedY` is -1 while it
  stands; `state tag=Player VelocityComponent` also shows what it stands on
  (`supportIndex`, `supportGeneration`) and how fast that goes (`supportVX`,
  `supportVY`). To see the ground and colliders, send `debug physics on`, then
  step and `drive_frame` (after `drive_start` with `gl: true`) or `capture`, or
  run with `JM_DEBUG_PHYSICS=1`.

## When the person points at something

From the editor they paste references: `scenes/level1.scene.json#Hero/Sword` is
an entity (a path of names in that scene; `Bat[2]` the second of that name),
`scenes/level1.scene.json@120,-40` a spot in the world (y up), and `(on the
ground of ...#Map)` says it's on that entity's terrain (a map's: the Tiled
objects of class `ground` or `platform` near there). Look there before
guessing: `debug physics on` and a frame show it.

## When the person has played

The person plays with `jm run`, and each play is recorded (`.jm/plays`). They
press F8 at moments that look wrong, then tell you about it. Look before you
guess: `jm plays show` (scenes, values over time, their markers and notes),
`jm plays frame latest m1` (what they saw at marker 1, replayed exactly),
`jm plays state latest m1 session` (the numbers then). After a fix, `jm plays
verify` says whether their play now goes differently, and `jm plays resume
latest m1` opens the game for them right at that moment to try it. All of it
is in `jm docs plays`, and over MCP as `play_show`, `play_frame`, ...

## AssemblyScript gotchas

- Number types are explicit: `i32`, `f32`, `f64`, `u32`. Use `<f32>x` to
  convert. Use `Mathf` for `f32` math; `Math` is `f64`.
- No `any`, no union types, and no `undefined`. Nullable is `T | null` for
  objects only. Closures can't capture local variables, so use module-level
  state or class fields.
- Each scripted entity runs its own instance, so module-level variables are
  that entity's state. Instances are expensive to create: keep scripts off
  bullets and particles, and give those `VelocityComponent` and
  `LifetimeComponent` instead.
- `assert((1 + 1) == 2)` fails: AssemblyScript folds all-literal comparisons
  wrongly inside `assert`. Real values are fine.
- Hooks are all optional: top-level code (setup), `onUpdate(dt: f32)`,
  `onCollide(other: Entity)` and `onMessage(m: Message)`.

## Reference

`jm docs` lists the topics, and `jm docs <topic>` prints one:

| Topic | Covers |
|---|---|
| `scripting` | the `@jm/runtime` API |
| `content` | every file format and component |
| `runtime-gameplay` | building blocks: menus, timers, projectiles, HUDs |
| `testing` | every `JM_*` variable, the driver, dumps and goldens |
| `networking` | multiplayer: sessions, shared entities, servers, `jm run --peers` |
| `plays` | the person's recorded plays: markers, replays, `jm plays`, Codex and ChatGPT |
| `editor` | the visual editor a person may open on the same files |
