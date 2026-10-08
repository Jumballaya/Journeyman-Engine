# Breakout: working on a Journeyman game

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

New files under `assets/` are picked up without editing the manifest. A new
scene does need adding to `scenes` in `.jm.json`. `jm generate list` shows the
starting templates; for example `jm generate script player` and
`jm generate prefab bullet`.

## The loop

1. **Look things up:**
   - `jm schema` lists every component's keys, with defaults and choices;
     `jm schema SpriteComponent` shows one.
   - The script API is in `jm docs scripting`, and its source is
     `assets/scripts/node_modules/@jm/runtime/index.ts` after the first build.
2. **Edit** the files.
3. **Build:** `jm build --json`. Each line is a problem, given as
   `level`, `category`, `message`, `file`, `line` and `column`. The last line
   is `{"result": "ok"|"failed", ...}`. Fix every error; warnings about
   scene keys are almost always typos. The first build downloads the script
   compiler if the machine has none, and `jm doctor` shows what it uses.
4. **Test logic:** run `jm test`. Each exported function in
   `tests/*.spec.ts` is a test, and `assert(cond, "message")` fails it.
   The game's state and its save live in memory there. Rendering and audio
   do nothing.
5. **Play it, headless and step by step.** The game only advances when told.
   Each line on stdin is a command, and each answer is one JSON line:
   ```sh
   printf 'step 60\npress Enter\nstep 120\nstate\nquit\n' | JM_DRIVE=1 JM_RENDERER=none jm run
   ```
   - `state` returns every entity with its components, the session state, the
     save, the UI layout and the frame's draw list.
   - Commands: `step [n]`, `state`, `down|up|press <Key>`, `set <key> <json>`,
     `scene <path>`, `capture <file.png>` (needs GL: use `JM_HEADLESS=1` instead
     of `JM_RENDERER=none`), and `quit`.
   - Keys use web names: `Enter`, `Space`, `ArrowLeft`, `KeyA`.
   - Errors the game logs come back in the next `step`'s `errors`. Add
     `JM_STRICT=1` to stop on the first one.
6. **Look at it**, if the machine has a display or software GL:
   ```sh
   JM_HEADLESS=1 JM_EXIT_AFTER_FRAMES=300 JM_CAPTURE_DIR=/tmp/f JM_CAPTURE_FRAMES=60,299 jm run
   ```
   Then view the PNGs. Once a screen looks right, record it with
   `jm golden --update` (`tests/golden/<name>.golden.json` lists the frames, an
   optional replay, and the scene to start in). From then on `jm golden`
   fails if it changes.
7. **Format:** run `jm fmt` before you commit. It writes the JSON layout the
   editor writes, so diffs stay small.

Runs are deterministic: headless runs use a fixed 1/60 s step and seed 1. The
same inputs give the same frames and the same state, so a failure you found
once can be replayed.

If a tool speaks the Model Context Protocol, `jm mcp` serves these same
commands over stdio. For Claude Code: `claude mcp add journeyman -- jm mcp`.

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
| `editor` | the visual editor a person may open on the same files |
