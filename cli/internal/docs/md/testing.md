# Testing & automation

## Game script tests (`jm test`)

From a project, `jm test` compiles `tests/*.spec.ts` (or the specs given) with
the project's AssemblyScript and runs every exported function as a test:

```ts
// tests/rules.spec.ts
import { Game } from "../assets/scripts/lib/game";

export function clearingFourLinesScoresATetris(): void {
  const game = new Game();
  // ...
  assert(game.score == 800, "a tetris scores 800");
}
```

`GameState` and `Save` are in-memory, `Data` reads the project's files, and
other engine calls do nothing, so it suits rules, data and state rather than
rendering. It extracts `@jm/runtime` (and script libraries) itself, so it works
before the first `jm build`. A failure prints the assertion message and line.
`jm test --json` reports like `jm build --json`: a line per test
(`{"test": "rules: scoresAdd", "result": "pass"}`, or an error with `message`,
`file`, `line`, `column`), then `{"result": "ok"|"failed", "passed", "failed"}`.

AssemblyScript 0.28 folds an all-literal comparison inside `assert` wrongly:
`assert((1 + 1) == 2)` fails. Real values (`assert(score == 800)`), a typed
literal (`<i32>(1 + 1) == 2`) or a `bool` local are fine.

## Running a game unattended

The engine reads these environment variables, which make it possible to
script a playthrough and check the result frame by frame.

**Runs are deterministic.** A frame runs on one thread in a fixed order, and
an automated run (`JM_HEADLESS` or `JM_INPUT_REPLAY`) steps a fixed 1/60 s with
seed 1 unless `JM_FIXED_DT` / `JM_SEED` say otherwise. The same build, replay
and seed draw the same frames, byte for byte (start from the same save:
`JM_SAVE_DIR` pointing at an empty folder). A run you play by hand gets a fresh
seed, which the log prints (`[Engine] seed 123...`); `JM_SEED` repeats it.
Two runs from the same recording can be compared frame for frame (`jm golden`,
below) or state for state (`JM_DUMP_DIR`).

| Variable | Effect |
|---|---|
| `JM_HEADLESS=1` | create the window hidden (rendering still happens), at the window's size on every machine (no 2x Retina framebuffer), so captures compare across platforms |
| `JM_RENDERER=none` | no window and no OpenGL at all: runs where there's no display or GPU (a CI container, a server); the game runs the same, frames are kept as data (dumps' `draw`) but there are no pixels to capture |
| `JM_FIXED_DT=0.0166667` | advance every frame by a fixed step |
| `JM_SEED=n` | the run's random seed: scripts' `Math.random` and the engine's own randomness (camera shake) all follow from it |
| `JM_EXIT_AFTER_FRAMES=n` | quit cleanly after `n` frames |
| `JM_CAPTURE_DIR=dir` + `JM_CAPTURE_FRAMES=60,120` | write those frames as `dir/frame_00060.png` |
| `JM_INPUT_REPLAY=file` | play key presses from a file (below); the real keyboard, mouse and gamepads are ignored |
| `JM_ENTRY_SCENE=scenes/x.scene.json` | start in another scene |
| `JM_SESSION=file.json` | set session values (scripts' `State`) before the first frame: with `JM_ENTRY_SCENE`, a deep link (the boss, with one life) |
| `JM_WATCH=1` | images, atlases, shaders and sounds reload when their files in the build change, live (`jm run --watch` sets it and rebuilds on every source change); scripts, scenes and UI need a restart for now |
| `JM_SAVE_DIR=dir` | keep `save.json` out of the player's real save directory |
| `JM_DRIVE=1` (+ `JM_DRIVE_RECORD=file`) | stepped by commands on stdin, answering on stdout (below); the record is the run's inputs as a replay |
| `JM_DUMP_DIR=dir` (+ `JM_DUMP_FRAMES=60,120`) | write the game's state as JSON: `dir/state_exit.json` at the end, and `dir/state_00060.json` at those frames (below) |
| `JM_ERRORS=-` or `JM_ERRORS=file` | every error as a JSON line, on stderr or into the file (below) |
| `JM_STRICT=1` | the first error ends the run, with exit code 1 |
| `JM_DEBUG_PHYSICS=1` | draws what physics sees over every frame: colliders (solid boxes magenta, others green, circles cyan) and terrain (solid white, one-way yellow) |
| `JM_RECORD_DIR=dir` | record the run as a play there (`jm run` does this when you play; `jm docs plays`) |
| `JM_PLAY_SESSION=dir` | replay a recorded play exactly (its seed, save, inputs and frame times); the state's `replay.diverged` says where it went differently |
| `JM_PLAY_UNTIL=n` | the replay stops at frame n (with the driver: yours from there) |
| `JM_PLAY_THEN=live` | after the replay, the player takes over (it fast-forwards there: nothing drawn or heard) |

Replay files have one event per line, `<frame> down|up <KeyName>`
(`#` starts a comment):

```
40 down Enter
42 up Enter
120 down Space      # hold fire from frame 120
```

Example — from a game's folder, start a game from a replay and capture a few
frames (`jm run` runs `build/` in the engine; the variables pass through):

```bash
jm build
JM_HEADLESS=1 JM_EXIT_AFTER_FRAMES=600 JM_SAVE_DIR=/tmp/jm-save \
JM_CAPTURE_DIR=/tmp/frames JM_CAPTURE_FRAMES=100,300,590 JM_INPUT_REPLAY=replay.txt jm run
```

`JM_HEADLESS` needs a display (or software GL: `xvfb-run` and Mesa on Linux).
Where there's none, `JM_RENDERER=none` runs the same game with no pixels: use
state dumps and the driver instead of captures.

**Driving a run.** With `JM_DRIVE=1` the game advances only when told: each
line on stdin is a command, and each gets one JSON line on stdout. Between
steps it waits, however long a tool takes to decide, and the fixed step keeps
the run reproducible: `JM_DRIVE_RECORD` writes its inputs as a replay file,
and replaying that file reaches the same state.

| Command | Does | Answers |
|---|---|---|
| `step [n] [dt]` | runs n frames (1 if not given), each dt seconds (default: the fixed step) | `{"ok": true, "frame": 180, "errors": [...]}` |
| `state [part...] [tag=Name...] [Component...]` | | `{"ok": true, "state": {...}}`, the state dump below; parts keep only those keys (`state session ui`), `tag=Paddle` only the entities with that tag, a component name only that component (`state tag=Ball TransformComponent`); an unknown part is an error |
| `get [tag=Name] <path>` | | one value: `get session.score`, `get scene`, `get tag=Ball TransformComponent.x` → `{"ok": true, "value": -52.4}` (`"values"` when several entities have the tag) |
| `down`, `up`, `press <Key>` | a key, seen from the next frame (`press` lets go after it); names as in input bindings (`A`, `Space`, `ArrowLeft`; see content's *Input bindings*) | `{"ok": true}` |
| `until [tag=Name] <path> <op> <value> [max n]` | steps until the value `get` would answer compares true (`<`, `<=`, `>`, `>=`, `==`, `!=`; a JSON value), or n frames (default 600) pass | `{"ok": true, "frame": 212, "steps": 40, "value": -271.5}`; not reached: `ok` false, with the last value |
| `echo <text>` | nothing: a label in a transcript | `{"ok": true, "echo": "<text>"}` |
| `near tag=Name [distance]` | what comes within distance (default 4) of that entity's colliders: colliders and drawn ground, nearest first; a gap below 0 is an overlap that deep | `{"ok": true, "near": [{"tags": ["Coin"], "kind": "box", "gap": 0.01}]}` |
| `set <key> <json>` | a session value, as scripts' `GameState` setters | `{"ok": true}` |
| `scene <path>` | loads a scene (on the next step) | `{"ok": true}` |
| `move x y`, `click [x y] [button]`, `mousedown`/`mouseup [x y] [button]`, `wheel dy` | the mouse, in logical px from the game's top-left (as UI rects in the state); buttons `left`, `right`, `middle`; a click lets go a frame later | `{"ok": true}` |
| `marker [note]` | a marker in the recorded play (`JM_RECORD_DIR`), as F8 makes | `{"ok": true, "marker": 1}` |
| `capture <path>` | the last frame as a PNG (needs GL) | `{"ok": true, "path": ...}` |
| `debug physics on\|off` | draws colliders and terrain over the frames from now on, as `JM_DEBUG_PHYSICS` does (the lines are in `state`'s draw list too, at z 1000000) | `{"ok": true}` |
| `quit` | ends the run | `{"ok": true}` |

The first line out is `{"ok": true, "ready": true, "frame": 0, "scene": ...}`. A step's
`frame` counts the frames run so far, which is also the next frame's number:
a key sent then is on that frame in the recording. A state's `frame` is the
frame it shows (the last one run), so after `step 300` it says 299 and equals
`JM_DUMP_FRAMES=299`'s dump of the replay.
A command that fails answers `{"ok": false, "error": "..."}` and the run goes
on; errors the game logs come back in the next `step`'s `errors` (as
`JM_ERRORS` writes them). Scripts' `log()` goes to stderr. For example, with
no window at all:

```bash
printf 'step 60\npress Enter\nstep 120\nstate session\nquit\n' | JM_DRIVE=1 JM_RENDERER=none jm run
```

**State dumps.** A dump is the game as data, for checking a headless run
without reading pixels:

```json
{"frame": 300, "time": 5.0, "scene": "scenes/level1.scene.json",
 "entities": [
   {"id": [12, 0], "tags": ["Player"], "parent": [3, 0],
    "components": {"TransformComponent": {"x": 120.5, "y": -40, "z": 5, "scaleX": 16, "scaleY": 16, "rotation": 0},
                   "ScriptComponent": {"script": "assets/scripts/player.ts", "failed": false}}}],
 "session": {"score": 1200}, "save": {"best": 4000},
 "ui": [{"entity": [5, 0], "root": {"tag": "body", "rect": [0, 0, 640, 360], "children": [
   {"tag": "div", "id": "score", "rect": [8, 8, 120, 20], "text": "1200"}]}}]}
```

Each component lists its script fields with the values scripts read (a
sprite's `shadow*` fields only while it has a shadow) (an
entity's name is one of its tags). `ui` is each shown document's elements as
laid out (logical px); an inline element (`<span id="score">`) has its own
`text` and the rect its words cover, so a value is found by its id. `draw` is what the frame drew: `world` sprites back to
front (`image`: its path, `"white"` for a solid quad, or a number for the engine's own textures such as text glyphs; `center`, `size`, `z`, and `rotation`/`color`/`texRect` when
not the default) and `screen` quads (UI and text, `rect` in logical px).
`camera` is where the game's camera looks (`x`, `y`), its `zoom`, and how much
of the world it shows (`width`, `height`). Frame N's dump matches frame N's capture.

A run with `JM_RENDERER=none` dumps the same state as one with OpenGL (the
engine's CI checks this), except screen quads' sizes: text is rasterized at
the display's pixel scale.

**Errors for tools.** With `JM_ERRORS`, each error the run hits (a script's
failed `assert` or trap, an asset, scene, prefab, image, sound or UI file that
won't load, ...) is a line of JSON:

```json
{"level":"error","category":"Script","message":"assets/scripts/walker.ts aborted: walker went too far at assets/scripts/walker.ts:9:3","file":"assets/scripts/walker.ts","line":9,"column":3,"frame":4}
```

`category` is the subsystem, `file` (and `line`/`column`) appear when known,
and `frame` is when it happened (0: while starting). With `JM_STRICT=1` the
run stops after that frame and exits 1, so a headless check fails on the first
problem instead of playing on. `jm build --json` reports the build the same
way: compile errors with file, line and column, scene and prefab problems,
and a last line `{"result":"ok"|"failed","errors":N,"warnings":N}`.

On exit the engine logs the average frame time (`[Engine] N frames in Xs (Y
ms/frame avg)`); in headless mode frames aren't vsync-limited, so that is the
real per-frame cost.

## Multiplayer runs

`jm run --peers N` runs a whole session on this machine, and `JM_NET_*`
variables host, join, and simulate latency and loss. `JM_REALTIME=1` keeps an
automated run to the clock, so it can talk to other processes in real time.
See [networking.md](networking.md#running-sessions-on-one-machine).

## Golden frames (`jm golden`)

A golden is a few frames of the game you've looked at and want to keep
looking that way. `tests/golden/<name>.golden.json` says how to reach them:

```json
{"replay": "boss.replay.txt", "scene": "scenes/boss.scene.json",
 "session": {"lives": 1}, "frames": [60, 300]}
```

`replay` (inputs, relative to `tests/golden`), `scene` and `session` (a deep
link) are optional; `frames` lists the frames to keep. `jm golden --update`
plays it headless from the build and records `tests/golden/<name>/frame_NNNNN.png`;
look at them, then commit them. `jm golden` plays it again and compares: a
frame passes when at most `maxDiff` (default 0.005, half a percent) of its
pixels differ by more than `threshold` (default 40 of 255) in a channel, so
different GPUs agree on the same game (macOS and Linux's software GL differ
by under 0.1%). Headless frames are the window's size on every machine, so
images recorded on one compare on another. A failing frame leaves its
capture and a diff image (changed pixels red) in `build/golden/<name>/`. The
run is strict: an error the game logs fails it too. `--json` reports like
`jm build --json`.

