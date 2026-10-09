# Friction log: newcomer builds Breakout with `jm` (agent, Linux container, no display)

Total: ~25 tool calls, all 5 features + all proofs done. Nothing blocked me for
long; the costs below are small, but each one is a place a weaker or less
patient agent could stall. Ordered most damaging first.

## 1. Driver `state` is all-or-nothing and huge, and nothing in the container can parse it
- **What happened:** `printf 'state\n' | JM_DRIVE=1 JM_RENDERER=none jm run` answers one JSON line with every entity, the whole UI tree and the full draw list (each glyph of "BREAKOUT" is its own quad). Several KB on the title screen, much more in-game with 50 bricks. `state Paddle` is accepted but the argument is silently ignored. The container has no `jq`, `python3` or `node` on PATH.
- **Expected / looked:** AGENTS.md step 5 and `jm docs testing` ("Driving a run") list `state` with no arguments and no filtering. `jm run --help` says nothing about the driver or any `JM_*` variable.
- **Cost:** ~5 calls. I wrote my own Node summarizer (`/work/tools/summ.mjs`) and ran it with jm's *managed* Node, which I found through `jm doctor` (`/root/.jm/toolchains/node-v24.21.0-linux-arm64/node`). That's a hack a newcomer shouldn't need.
- **Suggested fix:** `state <tag>` / `state session` / `state ui` / `get <tag> <Component.field>`; a `state --brief` with no `draw`; return `{"ok":false}` for unknown arguments instead of ignoring them. Or ship a tiny `jm json <expr>` since jq isn't guaranteed. Mention the driver and the main `JM_*` vars in `jm run --help`.

## 2. A driven run's recorded replay plays one frame off the driven run
- **What happened:** after `step 30` then `press Enter`, `JM_DRIVE_RECORD` wrote `29 down Enter / 30 up Enter`. Replaying it with `JM_INPUT_REPLAY=tests/playthrough.replay.txt JM_RENDERER=none JM_DUMP_FRAMES=710` gives Ball `(-54.4, 71.5)` at frame 710, while the driven run had `(-52, 78)` at frame 710. That is exactly one frame of ball travel. The outcome (score 60, lives 1, game over at 1910) matched, so a coarse check passes.
- **Expected / looked:** `jm docs testing`: "`JM_DRIVE_RECORD` writes its inputs as a replay file, and replaying that file reaches the same state." It doesn't.
- **Cost:** 1 call to notice. It would cost much more in a debugging session ("I recorded the bug, but the replay doesn't reproduce it").
- **Suggested fix:** fix the frame offset in the recorder or the replayer, and add a CI check (drive, record, replay, then diff the dumps).

## 3. `jm init "Breakout"` initializes the *current* directory; the argument is only the name
- **What happened:** `cd /work && jm init "Breakout"` wrote `.jm.json`, `scenes/`, `assets/`, AGENTS.md and the rest straight into /work. I expected a `Breakout/` folder, like `cargo new` or `npm create`.
- **Expected / looked:** `jm --help` does say "in the current directory", but `jm init [name]` reads like a folder name, and the "Next steps" output doesn't say where the files went.
- **Cost:** 2 calls (clean up, mkdir, init again).
- **Suggested fix:** print "Initialized Breakout in /work". Warn or refuse when the folder isn't empty. Or support `jm init <dir>` / `--dir`.

## 4. Docs are written for the engine repo, not for a game project
- `jm docs testing` opens with `./scripts/build-tests.sh`, `ctest` and `go test`. Its examples are `cd demos/strike_wing`, `../../build/release/engine/journeyman_engine .` and `scripts/check-determinism.sh`, none of which exist for a user. It never shows plain `jm run` with env vars outside the driver example.
- `jm docs runtime-gameplay` ends with "Verification: From the repository root … `node --test cli/internal/stdlib/tests/*.test.mjs`".
- `jm docs content`'s manifest example has `"engine": "../build/release/engine/journeyman_engine"`. `jm docs scripting` refers to "a Strike Wing enemy" and "the demo's `lib/session.ts`", which a project doesn't have.
- **Cost:** reading time only, but it made me doubt which commands apply to me.
- **Suggested fix:** move engine-dev material out of the `jm docs` topics, and write every example as a project user would run it (`jm run`, `jm build`).

## 5. Key-name conventions disagree between documents
- AGENTS.md says "Keys use web names: `Enter`, `Space`, `ArrowLeft`, `KeyA`." `jm docs content` (bindings) says "Keys use the `Key` enum names (`A`–`Z` …)", and the generated bindings file uses `"A"`. `jm docs testing` says driver keys are "names as in replay files". So for letters, is it `KeyA` or `A`? I only needed `Enter` and the arrows, so I didn't find out.
- **Suggested fix:** one key-name table, linked from all three places. Make the driver reject unknown key names loudly (if it doesn't already).

## 6. UI dump attributes inline text to the parent, not to the element's id
- **What happened:** the HUD is `<div>SCORE <span id="score">0</span></div>`, and the script calls `UI.setText("score", "60")`. In the `state` dump, the span `#score` has `rect [0,8,0,0]` and no `text`. The text sits on the anonymous parent div as `"SCORE  60"` (with two spaces). So I can't check `ui#score` by id; I had to key on the parent's rect.
- **Expected:** `jm docs testing` shows `{"tag": "div", "id": "score", ..., "text": "1200"}`, which implies text sits on the element with the id.
- **Cost:** 2 calls.
- **Suggested fix:** also report each inline element's own text (and rect) in the dump. Or document that inline runs fold into the block parent and tell users to use a block element for values they want to query.

## 7. Plain colored rectangles: undocumented
- Paddle, ball and bricks only need solid rectangles. No doc says what a `SpriteComponent` with no `texture` does. The draw list shows `"image": 1`, presumably a built-in white texture, so it probably works, but I couldn't confirm without pixels and `jm build` gives no hint. Draw-list `image` values are bare numbers with no legend mapping them to paths.
- **Suggested fix:** one line in `jm schema SpriteComponent` / content.md: "no texture = solid quad in `color`". Put the image path (or `"<white>"`) in the draw list.

## 8. No GL in the container: capture impossible, and the message is terse
- `JM_DRIVE=1 JM_HEADLESS=1 jm run` printed only `Journeyman: GLFW init failed` and exited. (With `JM_RENDERER=none`, `capture` correctly answers `"no pixels with JM_RENDERER=none (state has the draw list)"`, which is a good message.)
- **Suggested fix:** "GLFW init failed (no display?). Use JM_RENDERER=none for a headless run without GL". AGENTS.md could also say how to get software GL on Linux (e.g. `xvfb-run` + Mesa), since visual verification is otherwise impossible for agents in containers.

## 9. Generated script header points to a file that doesn't exist; the API source is spread across many files
- `jm generate script ball` writes `// API reference: docs/scripting.md, or node_modules/@jm/runtime/index.ts.` The project has no `docs/`; it should say `jm docs scripting`.
- AGENTS.md says the API source is `.../@jm/runtime/index.ts`, but index.ts only re-exports. The API is spread over about 30 files (entity.ts, state.ts, ...).

## 10. `jm build` copies tooling files as game assets and compiles library files as scripts
- The default `"assets": ["assets/**"]` makes `jm build` print `Copied asset: assets/scripts/asconfig.json` (and `package.json`, `tsconfig.json`), so build config ships in the game. It also prints `Built script: assets/scripts/lib/rules.ts`, compiling a hook-less helper module as its own script. Harmless, but noisy, and the game ships junk.
- **Suggested fix:** exclude `assets/scripts/*.json` and helper modules (`lib/`, or files without hooks) by default.

## 11. Small things
- Velocity: `jm schema VelocityComponent` lists script fields `vx`/`vy`, but the script API is `me.velocity.x`/`.y`. I grepped the runtime to find out which.
- How bindings get loaded isn't stated. Is any `*.bindings.json` under assets used automatically? Are several files merged? (The generated one worked without wiring, so it seems automatic.)
- `Key` must be imported from `@jm/runtime`. Obvious once you see it, but the scripting doc's `Input.keyPressed(Key.F11)` snippet shows no import. It cost one build cycle (`TS2304: Cannot find name 'Key'`, and the error was clear).
- `jm test` output is Node's test-runner format (`ℹ suites 0`, `ℹ todo 0`, `ℹ cancelled 0`). Fine, but noisy, and there's no `--json` like `jm build` has.
- `jm run` prints `Running engine: /opt/jm/journeyman_engine with build: build` on stderr every time. That's fine, but in drive mode it's easy to mix up if stderr is merged.

## What worked well
- AGENTS.md is genuinely good: the right length, a real build/test/drive loop, and the gotchas up front. It is the only doc I truly needed.
- `jm test` worked first try with zero setup. The toolchain was fetched silently and quickly; no Node install needed.
- `jm build --json` errors are precise (file/line/col plus the TS error code), and the last line is a clear result.
- The stepped driver is excellent once you can read its output: deterministic, simple commands, and an unknown command lists the valid ones. `JM_STRICT=1` gives a clean pass/fail. `JM_RENDERER=none` works in a bare container.
- `jm generate scene` registers the scene in `.jm.json` and says so. Asset globs mean no manifest edits for scripts, prefabs and UI.
- The engine-side collision sweep plus `onCollide`, `Overrides().tint()`, `GameState` and `UI.setText` all behaved exactly as documented. The game logic worked on the first run.
- `jm fmt` / `jm fmt --check` and `jm doctor` are short and clear.
