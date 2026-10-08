# Performance

Measure, change, measure again: `scripts/bench.py` records the engine's
numbers to `bench/results/<label>.json`, and compares two recordings. A
change made for speed or memory is judged by its before and after, both
committed with it.

```sh
git worktree add /tmp/before HEAD          # the code as it is
# ... make the change ...
scripts/bench.py run before --tree /tmp/before
scripts/bench.py run after
scripts/bench.py compare before after
```

Each run builds the tree optimized and records:

| Measurement | What it is |
|---|---|
| `<demo>_frame` | ms per frame over 2000 frames of `bench/replay.txt` (headless, fixed step, seed 1), median of 3 runs |
| `<demo>_peak_memory` | peak resident memory of that run, MB |
| `<demo>_startup` | launch to first frame, ms |
| `glyph_stress_peak_memory` | `bench/glyph_stress`: world text growing a pixel a frame for 1300 frames |
| `collision_<n>_colliders` | one collision frame with n moving colliders |
| `ecs_*` | getComponent / hasComponent / view costs at 10,000 entities |
| `layout_flex_depth_<n>` | laying out flex containers nested n deep |

The harness, replay and stress project always come from the current
checkout, so an old commit is measured exactly like a new one (the micro
benchmarks exist only from 2026-10-08 on). Numbers compare only on the same
machine; each file records which. Startup varies about ±8% run to run.

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
| Free assets' raw bytes after decoding? | demo assets 2.4–4.4 MB of 114–148 MB | not yet (~3%) |
| Parallel asset loading? | startup 200–250 ms including window and GL | not worth splitting every converter |
