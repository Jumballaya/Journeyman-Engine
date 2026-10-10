# Neon Vow

Kage, a cybernetic samurai, crosses the Silent Ward: a ruined neon city at dusk.
A weathered coat and luminous katana cut through indigo towers, teal haze and
amber shrine light. Moss-covered hills, eroded banks and root-bound ledges reclaim
the streets. Patrol sentries guard the route; a torii gate marks its end.
Built agent-first with `jm`, source files and a deterministic Pillow generator.

```sh
python3 tools/gen_art.py    # regenerate every image and the Tiled map
jm fmt
jm build && jm run
jm test                    # movement rules
python3 tools/check_level.py # geometry and driven traversal checks (requires JM_ENGINE / jm)
```

Start at the shrine checkpoint: `echo '{"checkpoint": 1}' > cp.json && JM_SESSION=cp.json jm run`.

**Controls:** arrows run, Space / Z jump (tap for a hop, hold for full height).
Jump buffering and coyote time forgive slightly early or late presses.
Down + Jump drops through a platform. Touch the end of a hanging cable in the
air to grab it; arrows pump the swing and Jump releases it. Land on a sentry
to disable it and bounce. Contact from the side returns Kage to the last
shrine lantern, with a brief period of blinking invulnerability.

## How it's made

| File | Role |
|---|---|
| `tools/gen_art.py` | one source for collision lines and painted ground; generates the dusk sky, distant skyline, ruined infrastructure, characters, cable and sparks |
| `tools/check_level.py` | checks curve slopes, jump clearances and Tiled alignment; drives both routes and saves command/response transcripts |
| `assets/maps/level1.tmj` | the Silent Ward: parallax image layers, ground and one-way platform lines, spawn, cable, sentry, checkpoint and goal markers |
| `art/kage/`, `art/sentry/` | 128×128 hero frames and 96×64 patrol sentry frames |
| `assets/atlases/characters.atlas.json` | linearly filtered character atlas, with three pixels of padding |
| `assets/scripts/hero.ts` | Kage's movement, jumps, cable grip, stomps, checkpoints and following camera |
| `assets/scripts/level.ts` | spawns cables and sentries from map markers |
| `assets/scripts/cable.ts`, `sentry.ts` | suspended cable rendering; sentry patrols with wall and ledge detection |
| `assets/scripts/lib/moves.ts` | pure movement rules checked by `tests/moves.spec.ts` |

The environment is painted at 2× and characters at 3×, then downsampled with
LANCZOS. Layered shading, fine surface marks, soft light and atmospheric haze
keep the palette cohesive without pixel-art scaling or cartoon outlines.
The katana is a visual element; combat remains the platformer's stomp mechanic.

Shape-preserving cubic curves are sampled at most 16 world units apart for both
the Tiled collision lines and the painted moss surface. The main hill reaches
(900, 280); shallow swales lead into a rounded 53-unit step and a 420-unit pit.
Only the step face, pit cliffs and boundary walls exceed 45 degrees. The two
one-way ledges sag or arch gently, and sit at most 155 and 132 units above the
ground below them. Roots, rounded stone masses, slack cables and foliage are
painted at 2×; the moss surface follows the exact sampled collision polyline.

Kage's 128px frames draw feet at image y=92; the sprite's ±48 scale maps that
to world y=-21, matching the collider's bottom
(half extents 16×16, offset 0,-5). Run frames keep a planted foot at that height.
Sentries keep their 18×12 collider, offset 0,-2; image y=60 at ±24×16 scale
puts their feet at y=-14. Session keys remain `checkpoint`, `falls`, `squashed`,
`holding` and `reachedGoal`.
