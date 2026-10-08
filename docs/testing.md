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
script a playthrough and check the result frame by frame:

| Variable | Effect |
|---|---|
| `JM_HEADLESS=1` | create the window hidden (rendering still happens) |
| `JM_FIXED_DT=0.0166667` | advance every frame by a fixed step (deterministic) |
| `JM_EXIT_AFTER_FRAMES=n` | quit cleanly after `n` frames |
| `JM_CAPTURE_DIR=dir` + `JM_CAPTURE_FRAMES=60,120` | write those frames as `dir/frame_00060.png` |
| `JM_INPUT_REPLAY=file` | play key presses from a file (below); the real keyboard, mouse and gamepads are ignored |
| `JM_ENTRY_SCENE=scenes/x.scene.json` | start in another scene |
| `JM_SAVE_DIR=dir` | keep `save.json` out of the player's real save directory |

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

On exit the engine logs the average frame time (`[Engine] N frames in Xs (Y
ms/frame avg)`); in headless mode frames aren't vsync-limited, so that is the
real per-frame cost.
