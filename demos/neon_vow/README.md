# Neon Vow

Kage, a cybernetic samurai, crosses the Silent Ward: a ruined neon city at dusk.
A weathered coat and luminous katana cut through indigo towers, teal haze and
amber shrine light. Moss-covered hills, eroded banks and root-bound ledges reclaim
the streets. Patrol sentries, a cable crossing and three sealed launch pods stand
between Kage and the far torii. Collect the ward's 20 light shards along the way.
Built agent-first with `jm`, source files and a deterministic Pillow generator.

Run from this folder, with `jm` on PATH and `JM_ENGINE` pointing at the engine.
Art generation needs Python 3 with Pillow installed; the checker uses only Python’s standard library.

```sh
python3 tools/gen_art.py     # regenerate every image and the Tiled map
jm fmt
jm build && jm run
jm test                     # pure movement, pod and run-state rules
python3 tools/check_level.py # headless geometry, traversal, HUD and restart checks
```

**Controls:** arrows / WASD run; Space / Z / gamepad A jump (tap for a hop,
hold for full height). Jump buffering and coyote time forgive slightly early or
late presses. Down + Jump drops through a platform. Touch a hanging cable's end
in the air to grab it; arrows pump the swing and Jump releases it.

Walk or fly into a launch pod to seal Kage inside. The glowing muzzle shows the
aim; the middle pod slowly sweeps. Press Jump again to fire. Steering and jump
release leave the launch momentum intact until landing or the next pod. A shrine
before the pod chasm saves the approach. The final pod lands on the torii's bank.

Land on a sentry to disable it and bounce. A side hit or pit fall costs one of
three lives and returns Kage to the last shrine, with brief blinking immunity.
Collected shards stay collected after a life is lost. Zero lives freezes the run
at Game Over; the torii freezes the timer at Level Clear. Enter / Jump restarts
from the beginning with three lives, all shards restored, and no checkpoints.
The HUD shows shards, lives and elapsed time; the result screen shows the shard
total and final time.

Deep links for review: session `checkpoint: 1` starts at the cable shrine;
`checkpoint: 2` starts at the pod shrine. For example:

```sh
mkdir -p .cache
printf '{"checkpoint":2}\n' > .cache/pods-session.json
JM_SESSION=.cache/pods-session.json jm run
```

## How it's made

| File | Role |
|---|---|
| `tools/gen_art.py` | collision curves and painted terrain, skyline, characters, cable, closed/active pods, shards, HUD icons and sparks |
| `tools/check_level.py` | checks geometry and drives both original sections, the pod chain, pickups, HUD, side hits, game over and restart; transcripts in `.cache/organic-review/` |
| `assets/maps/level1.tmj` | parallax art, ground/platform lines and spawn, cable, sentry, checkpoint, pod, shard and goal markers |
| `art/kage/`, `art/sentry/` | 128×128 hero frames and 96×64 sentry frames |
| `assets/atlases/characters.atlas.json` | linearly filtered character atlas with three pixels of padding |
| `assets/scripts/hero.ts` | movement, jumps, cable grip, stomps, shards, lives, checkpoints and camera |
| `assets/scripts/level.ts` | spawns cables, sentries, pods and shards from Tiled markers |
| `assets/scripts/pods.ts` | pod capture, aiming, sealed state and launch momentum |
| `assets/scripts/cable.ts`, `sentry.ts` | cable rendering and sentry patrols |
| `assets/scripts/hud.ts`, `assets/ui/hud.ui.html` | engine HTML/CSS HUD and paused result screen, with Enter/Jump restart |
| `assets/prefabs/pod.prefab.json`, `shard.prefab.json` | script-free pod and shard visuals |
| `assets/prefabs/launch.prefab.json`, `sparkle.prefab.json` | launch exhaust and pickup bursts |
| `assets/scripts/lib/` | pure movement, pod aim/velocity and run-state rules |
| `tests/*.spec.ts` | movement forgiveness, stomps, pod aim/launch and lives/clear/restart tests |

Pod markers have `angle` (degrees, counterclockwise from right), `rotate` (bool)
and `speed` (units/second) properties. Rotating pods sweep six degrees either side
of their base angle at a slow sinusoidal rate. The first pod is at (3890, 195),
then (4220, 370) and (4550, 455); the new bank begins at x=4830. The torii is at
x=5320 in a 5440×720 map. Edit geometry and markers in the generator, then rerun it.

The environment is painted at 2× and characters and props at 3×, then downsampled
with LANCZOS. Layered shading, fine surface marks, soft light and atmospheric haze
keep the palette cohesive without pixel-art scaling or cartoon outlines.
The katana is visual; combat remains the platformer's stomp mechanic.

Shape-preserving cubic curves are sampled at most 16 world units apart for both
the Tiled collision lines and painted moss surface. The original hill, rounded
53-unit step, 420-unit cable pit, and both gently curved one-way ledges remain.
Only the step face, pit cliffs and boundary walls exceed 45 degrees. Roots,
rounded stone masses, slack cables and foliage follow the same collision curves.

Kage's 128px frames draw feet at image y=92; the sprite's ±48 scale maps that
to world y=-21, matching the collider's bottom (half extents 16×16, offset 0,-5).
Sentries keep an 18×12 collider, offset 0,-2; their sprite feet are at world y=-14.

`state session` exposes `shards`, `shardTotal`, `lives`, `seconds`, `levelClear`,
`gameOver`, `checkpoint`, `falls`, `squashed`, `holding` and `reachedGoal`.
`pod` is 1–3 while sealed (0 otherwise); `podAngle`, `podFlying` and `podsLaunched`
support capture and traversal checks. Flags appear as 0/1 in driver dumps.
