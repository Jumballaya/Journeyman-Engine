# Testing & automation

## Unit tests

```bash
./scripts/build-tests.sh     # configures the `tests` preset, runs ctest, then `go test ./...` in cli/
```

C++ suites live next to each module (`engine/*/tests/`): ECS and scheduling,
assets and archives, scenes and spawning, game state, audio mixing, input
actions, HTML/CSS parsing and layout, atlases, post-effect chains, tile grids.

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

AssemblyScript 0.28 folds an all-literal comparison inside `assert` wrongly:
`assert((1 + 1) == 2)` fails. Real values (`assert(score == 800)`), a typed
literal (`<i32>(1 + 1) == 2`) or a `bool` local are fine.

## Script runtime and demo regression tests

Install the demo's AssemblyScript dependencies, then from the repository root:

```sh
node --test cli/internal/stdlib/tests/*.test.mjs
```

These compile the runtime and all demo scripts to WebAssembly, using explicit
host doubles to exercise timing, spawning, state, UI and gameplay transitions.
They complement the native rendered smoke checks below.

## Running a game unattended

The engine reads these environment variables, which make it possible to
script a playthrough and check the result frame by frame.

**Runs are deterministic.** A frame runs on one thread in a fixed order, and
an automated run (`JM_HEADLESS` or `JM_INPUT_REPLAY`) steps a fixed 1/60 s with
seed 1 unless `JM_FIXED_DT` / `JM_SEED` say otherwise. The same build, replay
and seed draw the same frames, byte for byte (start from the same save:
`JM_SAVE_DIR` pointing at an empty folder). A run you play by hand gets a fresh
seed, which the log prints (`[Engine] seed 123...`); `JM_SEED` repeats it.
`scripts/check-determinism.sh <build folder> <replay>` plays a game twice and
fails if any captured frame differs.

| Variable | Effect |
|---|---|
| `JM_HEADLESS=1` | create the window hidden (rendering still happens) |
| `JM_FIXED_DT=0.0166667` | advance every frame by a fixed step |
| `JM_SEED=n` | the run's random seed: scripts' `Math.random` and the engine's own randomness (camera shake) all follow from it |
| `JM_EXIT_AFTER_FRAMES=n` | quit cleanly after `n` frames |
| `JM_CAPTURE_DIR=dir` + `JM_CAPTURE_FRAMES=60,120` | write those frames as `dir/frame_00060.png` |
| `JM_INPUT_REPLAY=file` | play key presses from a file (below); the real keyboard, mouse and gamepads are ignored |
| `JM_ENTRY_SCENE=scenes/x.scene.json` | start in another scene |
| `JM_SAVE_DIR=dir` | keep `save.json` out of the player's real save directory |
| `JM_DUMP_DIR=dir` (+ `JM_DUMP_FRAMES=60,120`) | write the game's state as JSON: `dir/state_exit.json` at the end, and `dir/state_00060.json` at those frames (below) |
| `JM_ERRORS=-` or `JM_ERRORS=file` | every error as a JSON line, on stderr or into the file (below) |
| `JM_STRICT=1` | the first error ends the run, with exit code 1 |

Replay files have one event per line, `<frame> down|up <KeyName>`
(`#` starts a comment):

```
40 down Enter
42 up Enter
120 down Space      # hold fire from frame 120
```

Example — boot the demo, start a game, capture a few frames:

```bash
cd demos/strike_wing && jm build && cd build
JM_HEADLESS=1 JM_FIXED_DT=0.0166667 JM_EXIT_AFTER_FRAMES=600 JM_SAVE_DIR=/tmp/jm-save \
JM_CAPTURE_DIR=/tmp/frames JM_CAPTURE_FRAMES=100,300,590 JM_INPUT_REPLAY=../replay.txt \
  ../../build/release/engine/journeyman_engine .
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

Each component lists its script fields with the values scripts read (an
entity's name is one of its tags). `ui` is each shown document's elements as
laid out (logical px). Frame N's dump matches frame N's capture.

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
