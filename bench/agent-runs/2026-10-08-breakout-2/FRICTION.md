# Friction log: Breakout on Journeyman, as a newcomer agent

Container `jmbox`, no Node/GL/display. Everything learned from `jm --help`, `jm docs *`, `jm schema`, the generated AGENTS.md and templates. The whole game took about 30 tool calls, so most items are small. They're ordered by how much damage they would do, not by how much they cost me this time.

## 1. A misspelled prefab name only shows up at runtime, as 100 errors that point at `build/`
- **What happened:** probe on a copy, `spawn("brik", ...)` in a loop that spawns 50 bricks. `jm build --json` passed: `{"errors":0,"result":"ok"}`. The first driven `step` after the scene loaded then returned 100 errors, two for each spawn:
  `{"message":"File not found: build/brik"}` and `{"category":"EntitySpawner","file":"brik","message":"cannot load prefab 'brik': File not found: build/brik"}`
- **Expected:** a build-time check, since the name is a string literal and the short-name rule ("brick" resolves to assets/prefabs/brick.prefab.json) is known. Failing that, one error per distinct name, reported against the source file, with "did you mean 'brick'?" the way scene keys already get it. `build/brik` names the wrong place: the user never wrote that path.
- **Cost:** 2 calls in a probe. In a real bug the 100 lines bury the step output.
- **Fix:** when the short-name lookup fails, say so ("no prefab named 'brik' in assets (did you mean 'brick'?)"), de-duplicate repeats within a frame, and have `jm build` warn on literal names passed to `spawn`/`Scene.load`/`new Sound` that don't resolve.

## 2. No way to see a single pixel in a headless container
- **What happened:** `JM_HEADLESS=1 JM_EXIT_AFTER_FRAMES=5 JM_CAPTURE_DIR=/tmp/f JM_CAPTURE_FRAMES=3 jm run` gave
  `Journeyman: GLFW init failed: no display? JM_RENDERER=none runs without a window or GL (no frame captures), or give it one (xvfb-run on Linux)` (exit 1). The message is good, but the box has no xvfb or Mesa, and I wasn't allowed to install them. `jm golden` is unusable here too.
- **Expected:** some path to an image in the agent environment that AGENTS.md targets. AGENTS.md step 6 does hedge with "if the machine has a display or software GL".
- **Cost:** 1 call. The real cost is that I can't check that the UI text sits where I think, that colours are right, or that the HUD doesn't overlap the ceiling. I used the `draw` list instead (55 white quads = 3 walls + 50 bricks + paddle + ball), which only proves geometry.
- **Fix:** ship a software rasterizer for `JM_RENDERER=none` captures (the draw list is already data: solid quads, textured quads and glyphs), or bundle OSMesa/EGL-surfaceless. Or add `jm doctor` advice naming the packages to install.

## 3. Reacting to the game from the driver needs a coprocess plus sed/awk on JSON
- **What happened:** to show the paddle actually returning the ball (score rising while lives stay at 3), the script has to read the ball's x and choose keys. The box has no python or node (and `jm`'s managed node isn't on PATH), so `playtest.sh` became a bash `coproc` that sends `state tag=Ball`, pulls `"x":` out with `sed -n 's/.*"x":\(-\{0,1\}[0-9.]*\).*/\1/p'`, and compares floats with `awk`. It worked first time, but it only worked because the first `"x"` in the entity happens to be the transform's.
- **Expected:** a driver command that answers with a single value, e.g. `get tag=Ball TransformComponent.x` giving `{"ok":true,"value":-123.4}`. Or `step-until scene=scenes/gameover.scene.json max=3000`.
- **Cost:** about 2 calls of design and one long script. A less shell-fluent agent would spend many more.
- **Fix:** add `get <selector> <Component.field>` and `step until <condition>` to the driver. Document a minimal bash coproc pattern in `jm docs testing`.

## 4. Content warnings are hidden while a script fails to compile
- **What happened:** probe with a script compile error and a scene key typo (`"halfExtent"`) at the same time. `jm build --json` printed only the two script errors. The typo warning (`unknown key "halfExtent" (did you mean "halfExtents"?)`, which is excellent) appeared only on the next build, after the script was fixed.
- **Expected:** both reported in one build, because they are independent.
- **Cost:** 1 extra build cycle per occurrence.
- **Fix:** run content validation even when scripts fail. Also give the content warning a `line`: AGENTS.md says each line has `file`, `line` and `column`, but this one has `file` only.

## 5. Entity state is noisy, with no way to pick one component
- **What happened:** `state tag=Paddle` returns about 1 kB per entity. Most of it is `SpriteComponent` shadow fields on sprites that have no shadow (`"shadowAlpha":0.0,"shadowLayer":null,"shadowX":8.0,"shadowY":-8.0,...`), plus the collider and script.
- **Expected:** a way to ask for `TransformComponent` only, or defaults left out.
- **Cost:** 0 extra calls, but more tokens on every state read and fragile grep (see #3).
- **Fix:** `tag=Paddle:TransformComponent`, or leave out default or disabled sub-objects (an absent shadow).

## 6. JM_DUMP_DIR dumps are pretty-printed with every number on its own line
- **What happened:** `state_01593.json` is 44 KB for a 55-quad scene, because `"color": [\n 0.8,\n 0.8, ...` explodes every array. `grep -o '"session":{[^}]*}'` found nothing until I piped it through `tr -d " \n"`. The driver's `state` answer is compact, and the docs say jm's JSON keeps short numeric arrays on one line.
- **Cost:** 1 call.
- **Fix:** write dumps in the `jm fmt` layout or one-line compact JSON, the same as the driver.

## 7. `jm test` output is node:test output, with paths relative to node_modules
- **What happened:** a failing test prints `Error: bad: deliberate at ../../tests/bad.spec.ts:2:31` (relative to `assets/scripts/node_modules/.jm/`), followed by a node stack trace (`at TestContext.<anonymous> (file:///work/.../test-runner.mjs:91:29)` and more). There is no `--json`, though `build` and `golden` have one. The exit code is correct (1).
- **Cost:** 0 calls, some reading.
- **Fix:** report `tests/bad.spec.ts:2:31` from the project root, drop the runner frames, and add `jm test --json` with the same line format as build.

## 8. Small things
- The driver's `step` answer says `frame: N` (the next frame) while `state` says N-1. This is documented, but matching dump frames to driver states needs care. I used state frames throughout.
- `draw.screen[].image` is a number (`"image": 2`) for UI glyphs. `jm docs testing` says it is "its path, or "white"".
- `jm init` writes `scenes/main.scene.json`. A multi-scene game has to hand-edit `entryScene` and `scenes` in `.jm.json`, even though `assets` is a glob that needs no edits. I didn't check whether `jm generate scene` adds itself to `scenes`.
- The generated script template uses `Input.axis("left", "right")`, but `jm init` creates no bindings file, so those actions don't exist until `jm generate bindings`. I didn't check whether an unknown action warns.
- The default bindings template puts Enter and Space both on `confirm`. That's fine, but a task that says "Enter starts" needs its own action.
- There is no python or node on PATH in the box, so every JSON check is grep/sed. `jm` could expose a `jm json get <file> <path>` helper, or `jm doctor` could mention the managed node path.

## What worked well
- AGENTS.md is the right length and accurate. Every command it names exists, and the loop it describes (look up, edit, build `--json`, test, drive, fmt) is exactly what I did.
- `jm docs scripting/content/runtime-gameplay/testing` answered every API question. I needed no source reading, and the `@jm/runtime` API (`self`, `spawn` + `Overrides.tint`, `World.find/count`, `GameState`, `UI.setText`, `Scene.load`, `Input.axis/pressed`) did what the docs said on the first try.
- The first `jm build --json` succeeded with 0 errors and 0 warnings, and the toolchain downloaded itself silently (on stderr, so stdout stayed JSON).
- `jm schema <Component>` is precise, with kinds, defaults, hints and script fields.
- The driver is excellent: one JSON line per command, a clear error for an unknown part (`state has no 'nope' (parts: draw, entities, ...)`), and errors attached to the next `step`.
- Determinism holds. A second driven run recorded a byte-identical replay, and replaying it with `JM_INPUT_REPLAY` produced dumps byte-identical (`cmp`) to the driven run's dumps at 5 frames.
- The `jm test` setup is trivial: export functions, `assert`, no build needed, correct exit code.
- Error messages are good: did-you-mean on scene keys, a GLFW failure that names `JM_RENDERER=none`, and TS errors with file, line and column.
- `jm fmt` and `jm fmt --check` are quiet and predictable.
