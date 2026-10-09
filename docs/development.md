# Developing the engine

How to build and test Journeyman itself. (Making a game needs none of this:
see the README and `jm docs`.)

## Unit tests

```bash
./scripts/build-tests.sh     # configures the `tests` preset, runs ctest, then `go test ./...` in cli/
```

C++ suites live next to each module (`engine/*/tests/`): ECS and scheduling,
assets and archives, scenes and spawning, game state, audio mixing, input
actions, HTML/CSS parsing and layout, atlases, post-effect chains, tile grids.

## Script runtime and demo regression tests

From the repository root, with the compiler jm builds with (after any
`jm build`, or `jm doctor --fetch`):

```sh
export JM_ASC="$(cd demos/strike_wing && jm doctor --json | jq -r .toolchain.asc)"
node --test cli/internal/stdlib/tests/*.test.mjs
```

These compile the runtime and all demo scripts to WebAssembly, using explicit
host doubles to exercise timing, spawning, state, UI and gameplay transitions.
They cover the gameplay building blocks: serialization, timing, sequence
ordering, geometry, cross-instance checkpoints, menus, and entity generations.
The demo tests compile all actual scripts and exercise wave catch-up, result
skipping, weapon levels, respawn, boss phases, checkpoint restarts and all
background themes.

## End-to-end checks (CI)

Each takes a built demo (`jm build` in it first):

- `scripts/check-determinism.sh <build folder> <replay> [frames]` plays a game
  twice and fails if any captured frame differs.
- `scripts/check-null-renderer.sh <build folder> <replay> [frames]` compares
  state dumps with OpenGL and with `JM_RENDERER=none`.
- `scripts/check-drive-replay.sh <build folder>` drives the game while
  recording, replays the recording, and compares the states.
- `jm golden` in a demo with `tests/golden/` compares frames with the recorded
  ones.

Performance work: [performance.md](performance.md).
