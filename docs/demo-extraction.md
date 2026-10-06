# Demo script extraction review

The review covered every gameplay script and the shared helpers they import.
The aim is high-level scripts that describe Strike Wing, with reusable mechanics
in `cli/internal/stdlib/runtime`. Game scripts declare their own rules and data using library containers;
settings, screen setup, persistence and sprite shadows are library behavior.

## Coverage and decisions

| Script / area reviewed | Reusable mechanism now in runtime | Game-specific behavior retained |
|---|---|---|
| `player.ts`: movement, entry, weapon levels, bomb, death, collisions, pickups | `Input.vector`, `Vec2`, `Rect`, `lerp`, `Projectile`, `Timer`, `blink`, `World.destroyAll` | Speed/bounds, gun mounts, power progression, respawn/lives, shield durations, pickup rewards |
| `enemy.ts`: params, all seven flight patterns, six weapon modes, culling, drops, health/flash, collision handling | `Random`, `turnTowards`, `Rect`, projectile fans, timers, `Health`, `HitHistory` | Pattern parsing and flight choreography, aiming at an active player, fire/drop tuning and score |
| `boss.ts`: entry, three phases, aimed volleys, rings, spirals, escorts, hit feedback, death sequence | `Projectile.fire/fan/ring`, `Timer`, `Interval`, `Health`, `HitHistory`, `World.destroyAll` | Phase thresholds, sequencing, escort composition, music/signals and final reward |
| `director.ts`: stage selection, wave expansion/order, music, banner, warning, flash, exit | `Timeline<Launch>`, `Pulse`, `Timer`, `fadeOut`, `blink` | Stage completion conditions, warning timing, tracks/shaders and destination choice |
| `lib/waves.ts`: every stage formation | Timeline sorting and one-time dispatch | Named formation fields and all authored stage content; moved out of orchestration |
| `scroller.ts`: themes, tile geometry, decoration, cloud variation, spawn cadence | `TileGrid`, typed `Overrides`, `Random`, `Timer` | Ocean/land/storm art, density, scale, tint and speeds; engine ScrollWrap remains authoritative |
| `title.ts`: main/how-to/options, adjustment, attract formation, start/quit | `Menu`, random selection, `Timer`, `UI.fill`, `blink` | Screen routing, available options, V formation and session startup |
| `pause.ts`: focus loss, open/close, music ducking, restart, menu/quit | `Menu`; checkpoint restoration delegates through Session to `NumberSnapshot` | When pausing is allowed, music duck factor, action selection and scene choice |
| `hud.ts`: score/record, power, reserves, boss bar | `formatNumber`, `UI.showCount`, `UI.fill` | Element IDs, game state bindings, labels and seven-digit score width |
| `stage_clear.ts`: accuracy/no-miss calculation, reveal/skip, bonus, continuation | `Timeline<Result>`, `formatPercent`, `blink` | Bonus formulas, result contents, audio and next stage |
| `game_over.ts`: record display, input delay, continue/reset | `Menu`, `Timer`, number formatting | Continue cost, ship reset, progress retention and destinations |
| `victory.ts`: record/credits, delayed music, prompt, return | `Timer`, number formatting, `blink` | Jingle/music timing, credits and destination |
| Former `lib/game.ts` math/spawn/menu/shadow sections; current `lib/util`, `combat`, `presentation` | Math, random, formatting, typed overrides/JSON encoding, projectile geometry, menu navigation, transform following | Playfield constants, score width, explosion recipes, menu sounds and transition defaults; shadows are renderer-owned |
| `lib/session.ts`: all stored fields, stage reset, checkpoint, flash, high score | `Session`, `StateNumber<T>`, `StateFlag`, reset groups, `Checkpoint.captureOnce/restore/forget` | The run schema, bounded defaults, snapshot selection, run/continue rules and default record |
| `lib/presentation.ts`: settings and screens | `Settings`, live effect settings, `Screen`, `Panels`, `Audio.play` | Volume defaults, chosen CRT shader/uniforms, menu sounds and transition style |
| Plane shadows | `Sprite.shadow(options)`, native renderer ownership and prefab configuration | Offset, relative size, layer and tint |

## Interface choices

- Projectiles are named once, then used as `gun.fire`, `gun.fan`, and `gun.ring`.
  Scripts no longer calculate velocities or serialize component overrides.
- Menus return element IDs, so actions read as `choice == "p-resume"`; the demo
  configures its sounds through `Screen`, with navigation in the library.
- Timelines dispatch to top-level game functions with `update(dt, handler)`.
  Stage arrays and result enums remain easy to edit without changing scheduling.
- Cooldowns intentionally emit at most once per frame. `Interval` separately
  reports elapsed periods for effects that must preserve their phase.
- Health clamps values and reports a lethal hit once. Immunity and death
  sequences are still explicit game behavior.
- Hit history uses full entity handles and handles overlapping sources, not
  merely the most recent bomb or a recyclable entity index.
- Checkpoints use the host state store, not module memory, so scene reloads and
  separate pause/director script instances can share them.
- Visibility preserves the demo's `.hidden` CSS convention instead of mixing
  it with inline `display` removal.

A general enemy class, boss framework, automatic pause controller and scoring
system were not introduced. Their interface would
mostly describe this one game's rules. Asset names, stage data and balancing
constants also remain in the demo. Elapsed phase time and explicit game-state
flags are intentional where they describe behavior rather than timer mechanics.

## Verification

- Library WebAssembly tests (27 script tests total after the follow-up): math/analog magnitude, timer expiration/cancel,
  interval overshoot, stable timeline order/reset/skip, health, empty/wrapping
  menus, escaped/overwritten JSON, checkpoints across script instances,
  projectile geometry, tile layout, HUD styles, follower offsets and generations.
  New coverage checks scoped resets/defaults, bounds, cross-script checkpoints,
  persisted preferences, live effect toggles, panel switching, transition defaults,
  and configuring shadows without companion spawns.
- All 34 native renderer tests pass, including shadow transforms, opacity, layer
  selection and current animation UVs.
- Actual demo scripts compiled and executed against host doubles: all scripts
  start/update; result skipping awards once; waves catch up; all three weapon levels,
  bomb clearing and respawn work; bomb sources deduplicate; boss phases/death
  complete; pause restarts restore a director checkpoint; all background themes
  build the expected wrapping grids.
- `jm build` compiles all 11 scripts through the real generated entry points.
- Native hidden-window smoke checks cover all seven scenes. Captures inspected
  for title/options, pause/resume, stage-one shooting, land scenery/enemies,
  boss/health bar, stage results, game over and victory.

The automated host doubles do not model rendering or physics; native captures
cover their integration. These checks are not a full game-balance playthrough.
