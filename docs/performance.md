# Performance

Measure, change, measure again. `scripts/bench.py` measures the engine; a
change made for speed or memory is judged by its before and after.

**Deciding: `ab`.** Machine speed drifts 10-15% over hours (heat, background
work), more than many changes are worth, so before and after are measured
together: both trees built, rounds alternating between them (A B, B A, ...).

```sh
git worktree add /tmp/before HEAD          # the code before the change
# ... make the change in this checkout ...
scripts/bench.py ab --before /tmp/before   # prints before vs after; saves nothing
```

**Recording: `run`.** Once a change is finished and committed, record a
milestone: `scripts/bench.py run <label>` writes `bench/results/<label>.json`
(commit, date, machine); `scripts/bench.py compare <a> <b>` tables two.

Each round runs:

| Measurement | What it is |
|---|---|
| `<demo>_cpu_per_frame` | CPU time (user + system) per frame over 2000 frames of `bench/replay.txt` (headless, fixed step, seed 1) |
| `<demo>_frame` | the same run's wall time per frame (includes waiting on the GPU) |
| `<demo>_peak_memory` | peak resident memory of that run, MB |
| `<demo>_startup` | launch to first frame, ms |
| `glyph_stress_peak_memory` | `bench/glyph_stress`: world text growing a pixel a frame for 1300 frames |
| `sprite_stress_cpu_per_frame` / `_frame` | `bench/sprite_stress`: 4000 sprites alternating two textures (a texture change per sprite) |
| `sprite_batched_cpu_per_frame` | the same 4000 sprites with one texture (one draw) |
| `script_1000_entities_*` | `bench/script_stress`: 1000 entities each running a small script: CPU per frame (startup left out), startup CPU, peak memory |
| `collision_<n>_colliders` | one collision frame with n moving colliders |
| `move_among_<n>_colliders` | one `entity.move()` among n colliders, half of them solid |
| `ecs_*` | getComponent / hasComponent / view costs at 10,000 entities |
| `layout_flex_depth_<n>` | laying out flex containers nested n deep |

Every number is the median of 5 rounds. Since 2026-10-08 headless runs render
at the window's size on every machine (before, a 2x Retina framebuffer on
macOS), so `glyph_stress_peak_memory` (344 → 138 MB) and wall-clock frame
times from earlier recordings don't compare with later ones; CPU per frame
does. The harness, replay and stress
projects come from the current checkout, so old commits are measured exactly
like new ones (micro benchmarks exist only from 2026-10-08 on).

**Noise** (the same tree against itself, `ab --before . --after .`): demo
`cpu_per_frame` within ±5%, the stress tests within ±1.5%, demo wall-clock
frame time up to ±14% (don't decide on it). So: trust demo CPU changes beyond
±5% and stress-test changes beyond ±2%.

## Budgets

What a frame holds, from the measurements in `bench/results/budgets.json`
(Apple Silicon, optimized build). A 60 fps frame is 16.6 ms; plan on about
half of it for the CPU work below, leaving the rest to the GPU, the OS and
vsync. "Fits in 8 ms" extrapolates the measured cost linearly; beyond the
measured counts it's an estimate, so measure your own worst scene with
`scripts/bench.py` before relying on it.

| Thing | Measured | Per unit | Fits in 8 ms (estimate) |
|---|---|---|---|
| Sprites sharing a texture | 4000: 0.53 ms/frame (whole run, engine included) | under 0.13 µs | tens of thousands; the GPU's fill rate decides first |
| Sprites changing texture every sprite (worst case) | 4000: 3.2 ms/frame | 0.8 µs | ~10,000; use atlases, so neighbors share a texture |
| Scripted entities (a small `onUpdate`) | 1000: 3.0 ms/frame | 3 µs a frame | ~2,500 |
| Starting a scripted entity (spawn) | 1000: 242 ms | 0.24 ms each (0.85 ms for a demo-sized script) | spawn a few a frame, or pool them |
| Script memory | 1000: 335 MB peak | ~0.2 MB each (its own wasm runtime) | memory runs out before time does |
| Colliders (overlap reports) | 2000: 0.20 ms; 5000: 0.83 ms | ~0.1–0.17 µs, a little worse than linear | ~20,000 |
| `entity.move()` among solids | 0.33 µs among 100 colliders, 5.8 µs among 2000 | linear in colliders | a few hundred moves a frame in a 2000-collider level |
| UI relayout (a style or text change) | worst demo screen 0.18 ms, mean 14 µs | per change, not per frame | relayouting every frame is affordable for screens like the demos' |
| UI flex nesting | 4–12 µs at depth 2–6, 0.49 ms at depth 12 | grows fast past depth 8 | keep flex nesting under ~8 deep |
| Text sizes in use (glyph cache) | 1300 sizes in a row: 138 MB peak at 1x (344 MB at 2x) | at most 4 pages per filter, then it starts over | any number, at a re-rasterizing cost when sizes churn |

Hard limits: a script call (`onUpdate`, `onCollide`, a message) may take up to
25 million wasm steps before it's stopped as a runaway (the demos' largest
use about 420 thousand); a frame's step is at most 1/20 s (a slower frame
slows the game instead of skipping physics). Demo games, for scale: 0.32–0.47
ms of CPU a frame, 114–147 MB, 160–200 ms to the first frame.

## Results

Apple Silicon (arm64), macOS 26, optimized builds.

### Engine core work, 2026-10 (`before-cc926ea` → `current`)

From before Adapt's review fixes to after: the single-threaded frame,
determinism, optimized scripts (`jm build --optimize`), swept sort-and-sweep
collision, the glyph cache budget, creation-ordered archetypes.

| | before | after | |
|---|---|---|---|
| Strike Wing frame | 0.58 ms | 0.43 ms | −26% |
| Tetris frame | 0.51 ms | 0.37 ms | −28% |
| jrpg / platformer / dungeon frame | 0.36–0.37 ms | 0.27–0.29 ms | −22 to −25% |
| Ash and Iron frame | 0.40 ms | 0.39 ms | −2.5% (little of it is scripts) |
| Glyph stress peak memory | 11,319 MB | 345 MB | 33x less |
| Demo startup | 192–253 ms | 200–234 ms | noise |
| Demo peak memory | 114–148 MB | 114–147 MB | unchanged |

### Decisions the numbers made

| Question | Measured | Decision |
|---|---|---|
| Is the worker pool worth it? | inline frame 5–10% faster than threaded (Strike Wing 0.60–0.67 vs 0.63–0.75 ms) | single-threaded frame |
| What does spawning a scripted entity cost? | start 1.75 ms (parse 0.11, start function 1.64); optimized 0.85 ms | `jm build` optimizes; sharing parsed modules (0.09 ms) not worth it |
| Collision: check every pair? | 2000 colliders 11.3 → 2.1 ms, 5000: 66 → 7.6 ms (debug build) | sort and sweep, same pairs in the same order |
| ECS hot paths? | getComponent 3.5 ns, view iteration 2.2 ns/entity at 10k | no work needed |
| Flex layout nesting is exponential: fix? | 4–12 µs at depths UIs use, 0.47 ms at 12 | not yet: reusing the measure pass would change some layouts |
| UI: restyle and relayout the whole document on any change? | demo screens 14 µs mean, worst 212 µs (Ash and Iron's HUD), which relayouts every 10-20 frames: ~0.015 ms/frame | not worth incremental restyling |
| Renderer: re-upload instances per texture run? | sprite stress 4.80 -> 3.20 ms/frame CPU uploading once per pass (A/B) | one upload per pass; uniforms were already per pass |
| Script fuel: per-iteration yield check cost? | no difference beyond noise (A/B, and alternating Strike Wing runs) | fuel on, 25M steps per call |
| Entity blocking: index the solids? | `move()` 0.6 µs among 100 colliders, 6.8 µs among 2000 (it scans them all); demos unchanged (A/B) | not yet: 20 movers in a 2000-collider level is ~0.14 ms/frame |
| Free assets' raw bytes after decoding? | demo assets 2.4–4.4 MB of 114–148 MB | not yet (~3%) |
| Parallel asset loading? | startup 200–250 ms including window and GL | not worth splitting every converter |
