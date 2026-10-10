# FRICTION: building "Hills" with Journeyman as a newcomer agent

Total: about 32 tool calls from `jm --help` to the final `jm fmt`. Most damaging first.

## 1. The driver can't wait for a condition, so I wrote a bash wrapper (AGENTS.md says not to)
- **What happened:** the lift moves by itself, so "jump on when it's at the bottom" depends on timing. The driver only has fixed `step n`. Piping `printf ... | jm run` can't branch, so I wrote `.jm/play.sh`, a bash `coproc` that sends one command, reads one answer and loops `step 1; get tag=Lift TransformComponent.y; get ... vy` until `y < -270 && vy > 0`.
- **Expected / looked:** `jm docs testing` (driver table), AGENTS.md "The loop". AGENTS.md says "Reach for jm's own commands ... before writing a helper script: a wrapper around them is a second toolchain". The driver left me no other way.
- **Cost:** about 6 calls (writing the wrapper, a `;;` syntax slip, awk post-processing to make the transcript readable).
- **Fix:** add a driver command like `until <get-path> <op> <value> [maxFrames]` (e.g. `until tag=Lift TransformComponent.y < -270 max 600`), plus `echo <text>` so a transcript can label its sections. MCP `drive` could take the same.

## 2. The camera isn't in the state, and `get` can't read dotted session keys
- **What happened:** to prove "the camera follows the player":
  ```
  get camera      -> {"error":"state has no camera","ok":false}
  state camera    -> {"error":"state has no 'camera' (parts: draw, entities, frame, net, save, scene, session, time, ui; ...)"}
  ```
  So I added game code that copies `Camera.x/y` into the session (`GameState.setNumber("debug.camX", ...)`), and then:
  ```
  get session.debug.camX -> {"error":"state has no session.debug.camX","ok":false}
  ```
  even though `state session` showed `{"debug.camX":-60.0,...}`. The dot is read as a path and there's no way to escape it. I renamed the key to `camX`.
- **Expected:** a `camera` part (x, y, zoom, width, height). The doc's own example key style (`item.` prefixes in `GameState.keys("item.")`) encourages dotted keys.
- **Cost:** 4 calls, plus debug code left in the shipped player script.
- **Fix:** add `camera` to the state dump and the draw list. Make `get session.<key>` try the whole remaining string as a key before splitting on dots (or support `session["a.b"]`).

## 3. Drawn ground is invisible unless physics debug is on
- **What happened:** a `TerrainComponent` with chains collides perfectly but draws nothing, so the level is a void in a normal run. I generated 11 rotated solid-quad sprites (one per segment: midpoint, length/2, atan2) with awk in the container and pasted them into the scene. Each ground line now exists twice and has to be kept in sync by hand.
- **Expected / looked:** `jm schema TerrainComponent` (only `chains`, `layerMask`), `jm docs content` "Drawn ground" (it assumes painted Tiled art underneath).
- **Cost:** 2 calls plus the risk of the two going out of sync.
- **Fix:** an optional `stroke: {color, width}` (and maybe `fill`) on `TerrainComponent`, so the line is the art for prototypes.

## 4. A near-miss collision is silent, and the tools can't show why
- **What happened:** the coin was never collected while riding the lift (`score` stayed 0, no errors). I had to add `log()` to `onCollide`, replay, then sample positions per frame to find the cause: the player landed at x=981.99 (right edge 989.99) and the coin's box starts at 990, a 0.01 miss. That was my level's fault, but nothing pointed at it.
- **Expected:** a driver query such as `overlaps tag=Player` or `contacts`, or `Physics.overlapBox` exposed as a driver command. Physics debug drawing needs GL, which a no-GPU box doesn't have at first.
- **Cost:** 4 calls.
- **Fix:** add a driver `query box x y hw hh` / `contacts tag=X` command that answers with entities and their distances, so "almost touching" is visible as data.

## 5. Particle effects can't be checked from the state
- **What happened:** `state tag=sparkle ParticleEmitterComponent` gives `{"angle":90,"burst":0,"emitting":1,"rate":0}`, with no count of live particles (the burst value reads 0 once it has been used). To prove the burst happened I counted `"center"` entries in `state draw` (53 = 13 level sprites + 40 particles).
- **Cost:** 2 calls.
- **Fix:** expose a read-only `alive` (live particle count) on `ParticleEmitterComponent` in the state and scripts.

## 6. `jm init "Hills"` initializes the current directory, not `./hills`
- **What happened:** from `/work`, `jm init "Hills"` printed `Initialized "Hills" in /work` and wrote `.jm.json`, `scenes/`, `assets/` and so on into `/work`. I moved them into `hills/` by hand.
- **Expected:** a new folder named after the project, or at least a note in `jm init --help` (I didn't read it first, which was my fault). The name looks like a folder argument.
- **Cost:** 1 call.
- **Fix:** `jm init <name>` creates `./<name-slug>/` unless `--here` is given, or it warns when the current directory isn't empty.

## 7. Docs point at the engine repo and at names that don't exist in a project
- `jm docs testing` mentions `scripts/check-null-renderer.sh` and `scripts/check-multiplayer.py <jm> <demo>`, which aren't in the project or on PATH.
- The driver table says `set` works "as scripts' `State.set`", but the scripting API is `GameState`.
- `jm docs scripting` explains timing with "a Strike Wing enemy" and "the demo", which a newcomer has never seen.
- The docs example uses `Input.pressed("jump")`, but the default `input.bindings.json` has no `jump` action. I added one. I didn't check what an unbound action does. If it returns false with no error, that's a silent trap.
- **Cost:** about 0 calls (I worked around them), but each one cost reading time.
- **Fix:** strip repo-internal references from `jm docs`, rename `State.set` to `GameState`, add `jump` to the init bindings (or warn at build time about actions scripts use that no binding defines).

## 8. Small oddities
- The `JM_DRIVE_RECORD` output isn't in frame order: `844 down Space / 845 up Space / 844 down ArrowRight`. It replays correctly anyway.
- The driver's ready line has `"plays":2`, which isn't documented and is confusing in a driven run.
- `jm generate prefab` writes `{"components": {}, "tags": []}`, which doesn't hint at what goes inside. The script and UI templates are much more helpful.
- Getting PNGs means installing apt packages (`xvfb libgl1-mesa-dri libglx-mesa0`, about 1 slow call). AGENTS.md says so exactly, which is good, but `jm doctor` could offer `--fetch-gl`.

## What worked well
- **AGENTS.md's "Platformers and moving things" section was spot on.** The player (`motion: walk`), the one-way chain (`oneWay: true`) and the lift (`motion: move` + `blocksMask: 1`) all worked on the first build. The player walked up and down the hill grounded on all 220 frames and rode the lift with `supportVY` reported.
- `jm build --json` / `jm test` were fast and clear. The toolchain downloaded itself on first use, and the tests ran with no build.
- Driver `get` / `state tag=X Component` filters keep the output small. Unknown parts answer with the list of valid ones.
- Determinism: a driven run records a replay, and `JM_INPUT_REPLAY` + `JM_DRIVE` resumes at exactly the same frame. That's how I bisected the coin bug and took the PNGs.
- `CameraFollow`, `GameState`, the HUD's HTML and `UI.setText`, and the particle prefab recipe all worked first try straight from the docs.
- `debug physics on` + `capture` under `xvfb-run` produced clear frames.
- `jm schema` is accurate and has defaults (which is how I learned `blocksMask` defaults to 0).
