# Gameplay building blocks

`@jm/runtime` provides reusable mechanics as well as engine access. Keep game
rules, asset choices, and presentation in your scripts; use these modules for
the bookkeeping. All examples below use the live entity interface.

## Projectiles and companions

```ts
import { Projectile, PI, World } from "@jm/runtime";

const gun = new Projectile("enemy_bullet", 190);
gun.fire(x, y, aim);
gun.fan(x, y, aim, 3, 0.44); // three shots, 0.44 radians total spread
gun.ring(x, y, 18);         // no duplicate shot at the end of the circle
World.destroyAll("enemy_bullet");
```

`new Projectile(prefab, speed, true)` rotates the sprite toward travel. The
optional fourth argument is the direction the unrotated art faces (default
PI/2, up). `scatter(x, y, count, minSpeed)` chooses random directions and speeds
between minSpeed and the projectile's configured speed, for sparks/debris.
Zero shots do nothing; a one-shot fan aims straight down its center angle.

`TransformFollower(child, dx, dy, copyRotation = true)` follows a `Transform`.
Call `follow(owner.transform)` each frame and `destroy()` when the owner dies.
It tolerates the frame before a spawned child's components exist. It owns only
the companion; destroying it never destroys the owner.

`Health(maximum)` exposes `current`, `max`, `fraction`, and `dead`. `damage(n)`
returns true only for the hit that kills; `heal(n)` clamps to maximum. Callers
handle immunity, sounds, scoring and death sequences. `HitHistory.accept(entity)`
returns true only on the first encounter with each live source, useful for area
damage. It retains full entity generations and prunes destroyed sources.

## Time and sequences

```ts
const reload = new Timer(); // initially ready
reload.tick(dt);
if (reload.ready && Input.down("fire")) {
  gun.fire(x, y, aim);
  reload.start(0.1);
}
```

`Timer(seconds)` starts a delay. `tick(dt)` returns true once when the delay
expires. `start(0)` fires on the next tick; an unstarted or cancelled timer never
fires. `remaining`, `ready`, `extend(seconds)` and `cancel()` cover shields,
cooldowns and delays. Timers accept the script's dt, so gameplay timers pause
with the script and pause-menu timers use its unscaled dt. Restarting a timer
deliberately discards overshoot (at most one action per frame).

`Interval(seconds).tick(dt)` preserves overshoot and returns the number of
elapsed periods. Loop for full catch-up, or check `> 0` to coalesce missed
periods into a single visual effect. `reset()` restarts its phase.

```ts
const events = new Timeline<string>()
  .at(1, "kills")
  .at(1.5, "accuracy")
  .at(2, "bonus");

// In onUpdate:
events.advance(dt);
let event = events.take();
while (event !== null) {
  reveal(event.value);
  event = events.take();
}
```

Events are sorted by time, with insertion order preserved at equal times.
`take()` consumes each event once, including after a large dt. `finish()` makes
all remaining events due (skip an animation); `done` reports exhaustion.
`reset()` rewinds for replay. Add events before playing/consuming the timeline or after resetting.
For high-level scripts, `update(dt, handler)` advances and dispatches all due
events to a top-level function. For example, `waves.update(dt, launch)` hides
the polling loop. AssemblyScript handlers cannot capture local variables;
top-level game functions can access their script's module state.

`Pulse(decayPerSecond)` holds a numeric visual impulse. `trigger(strength)`
keeps the strongest overlapping request; `tick(dt)` decays to zero. `blink(t,
rate, high, low)` alternates values; rate counts switches per second.
`fadeOut(t, hold, duration)` provides a clamped opacity after a hold.

## Menus and HUDs

```ts
const menu = new Menu(["resume", "restart", "quit"])
  .sounds(new Sound("move"), new Sound("select"));
menu.render();

// In onUpdate, only for the active menu:
if (menu.update() == "resume") resumeGame();
```

A menu returns an element ID on confirmation, or an empty string. `selected`
returns the current ID; `index` accesses its clamped numeric index;
`select(index)` also renders. `previousAction`, `nextAction`, `confirmAction`
and `selectedClass` default to `up`, `down`, `confirm`, and `selected`.
Empty menus are inert. Opposite directions cancel. `handle(previous, next,
confirm)` allows a caller-supplied input source.

`UI.fill(id, fraction)` and `UI.opacity(id, amount)` clamp to 0..1.
`UI.showCount("life", count, total, "hidden")` controls life1..lifeN.
`UI.setVisible(id, visible, "hidden")` toggles a CSS class, including documents
that use `display: none !important`. Without a class argument it keeps the
existing inline-display behavior.

## State and spawning

`Store.getBool/setBool` store flags. `record(key, candidate, fallback)` updates a
numeric record only when exceeded, returning whether a new record was set.
`takeNumber(key)` reads and removes a numeric request shared between scripts.

`NumberSnapshot(store, prefix, keys)` captures selected numeric keys into the
host store. `capture()` records values and absence; `restore()` restores them
and returns false if never captured. A fresh script instance with the same
prefix and keys can restore the checkpoint. Reserve the prefix's namespace for
this snapshot (including its `.captured` marker). Game rules decide when to
capture and which keys belong in the checkpoint.

`Overrides` has typed `texture`, `tint`, `scale`, `rotation`, `velocity`,
`lifetime`, `scrollY`, `param`, and `paramText` methods. Repeated setters replace
previous values, siblings remain intact, and strings are JSON-escaped. Numeric
values must be finite. `text(component, property, value)` and `json(component,
property, rawJson)` extend this to custom components; script params use the
parameter methods. `toJson()` does not mutate the builder. Overrides are
applied during deferred spawning; writing live views immediately after spawn
cannot substitute for them.

`tileGrid(prefab, grid, overrides)` lays out tile centers. The `TileGrid` value
uses named fields: `{ columns: 15, rows: 22, x: -224, y: -336, width: 32,
height: 32 }`. Supply scrolling, scale, texture and tint through overrides. Wrapping
remains the engine's `ScrollWrapComponent` behavior.

## Small value helpers

- `Vec2.set(...).limit()` caps magnitude without amplifying partial analog input.
  `Input.vector(left, right, down, up, out)` fills a reusable vector.
- `Rect(left, bottom, right, top)` provides inclusive `contains`, `clampX` and
  `clampY` in world coordinates.
- `clamp`, `lerp` (clamped weight), `angleTo`, `angleDifference`, and
  `turnTowards` use f32/radians, with zero along +x and PI/2 along +y.
- `Random.range`, `int` (inclusive endpoints), `pick` (nonempty arrays), and
  `chance` use the engine-seeded random source.
- `formatNumber(value, minimumDigits)` floors nonnegative values and zero-pads
  without truncating larger numbers. The game chooses score width.
  `formatPercent(fraction)` clamps to 0..1 and rounds to a whole percent.

## Verification

From the repository root, after installing the demo's script dependencies:

```sh
node --test cli/internal/stdlib/tests/*.test.mjs
```

The tests compile the library to WebAssembly and exercise its public interfaces
against explicit host doubles. They cover serialization, timing, sequence
ordering, geometry, cross-instance checkpoints, menus, and entity generations.
The demo tests compile all actual scripts and exercise wave catch-up, result
skipping, weapon levels, respawn, boss phases, checkpoint restarts and all
background themes.

## Shared sessions, settings, and screens

`Session` declares live, typed values backed by `GameState`. It never caches a
copy in a script instance. Defaults, numeric limits, increments, records, and
reset groups are part of the declaration:

```ts
const run = new Session("run.");
const ships = run.group();
const score = run.number<f64>("score", 0);
const lives = ships.number<i32>("lives", 2, -1, 5);
const won = run.flag("won");
const checkpoint = run.checkpoint("stageStart", [score, lives, won]);
score.add(100);
lives.value--;
checkpoint.captureOnce(stageNumber); // retries retain the first attempt
checkpoint.restore();               // works from another script or scene
ships.reset();                      // only these declarations, to their defaults
checkpoint.forget();                // a continue can capture a fresh attempt
```

Prefixes are literal: include a separator such as `"run."` when desired. Empty
prefixes and groups let you adopt existing keys. Reserve checkpoint names for
their metadata. `reset()` removes declared values (and child groups), leaving
unrelated keys and checkpoints alone. A fresh handle sees the declared default
when its key is absent. `StateNumber.take()` returns the current value and resets
it; `record()` keeps the largest value. `StateNumber` and `StateFlag` may also be
constructed directly with another `Store`, such as `Save`. Numeric writes ignore
non-finite values; an optional step rounds before bounds are applied.

`Settings` supplies persisted music/SFX volumes and fullscreen, with custom
numeric/flag settings inherited from `Session`. Volume setters apply immediately.
An effect setting connects a saved flag to a custom shader without duplicating
creation and enable/disable code in each screen:

```ts
export const settings = new Settings({ musicVolume: 0.6, sfxVolume: 0.8 });
export const crt = settings.effect("crt", "crt").setFloat("u_strength", 1);
export const screen = new Screen({
  settings: settings, transition: "wipe", seconds: 1, hiddenClass: "hidden",
  menuMove: "menu_move", menuConfirm: "menu_select",
});
```

Call `screen.open()` from the scene controller, then `screen.update()` each frame
(including paused frames if fullscreen should remain available). Opening applies
saved audio/window preferences and creates declared effects once per settings
instance. Updating handles the fullscreen action and observes effect changes
from other scripts. `crt.value = false` applies immediately in the owning script.
Use a single screen controller per scene; overlays may import the configuration
and use `screen.menu()`, `screen.setVisible()`, and `screen.goTo()` without opening
another set of effects. `screen.panels(ids).show(id)` makes one panel visible, or
hides all for an empty ID. Transitions ignore requests while one is active.

`Audio.play("pickup", 0.8)` plays a one-shot; use `Sound` or `Music` when you need
to retain playback controls.
