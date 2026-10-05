# Engine gaps found building Hollow Grove

What a top-down adventure needed that the engine or `@jm/runtime` doesn't
offer, and how this project works around it. **High** blocks a feature or is
a bug, **med** forces an awkward workaround, **low** is a nice-to-have.

| # | Gap | Severity | Workaround here |
|---|---|---|---|
| 1 | **Bug:** `Camera.setPosition` moves the view twice as far (see demos/platformer/GAPS.md #1). | high | `lib/view.ts` halves the position, marked TODO. |
| 2 | No tile maps: ~1000 tile entities per area and script-side collision. | med | ASCII maps (`lib/areas.ts`), `lib/body.ts` with corner sliding. |
| 3 | A script can't read another entity's params or data. | med | Each side owns its part: items apply themselves on touch; the hermit runs his own dialog; the hero publishes its room through GameState for the area. |
| 4 | No messaging between scripts. | med | Tags as mailboxes (the hero tags the hermit `talk`); one-time world changes as `Session.done()` keys that doors poll. |
| 5 | A trapped script is logged as "entity 0:1" with no script name or line. | med | Guessing from the message (here: an empty string split in `area.ts`). |
| 6 | `Scene.transition` is silently ignored while one is running; a script that marks itself "leaving" then waits forever. | med | Menus check `Scene.transitioning` before acting. |
| 7 | Collision pairs need a `VelocityComponent` on one side, even for script-moved bodies. | low | Zero velocities on every actor prefab. |
| 8 | `Sprite.play()` restarts the animation; no way to read the current one. | low | `playOnce()` in `hero.ts`. |
| 9 | `Overrides` can't add tags to spawned entities. | low | Doors and items carry their map position as a param. |

Worked well: the HTML HUD (hearts swapped with `UI.setAttribute(src)`), typed
dialog over a paused world (`runWhenPaused`), per-room spawning with
`World.findAll`, short sound/prefab names, the sword as a short-lived
collider entity.
