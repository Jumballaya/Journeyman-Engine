# Engine gaps found building Hollow Grove

What a top-down adventure needed that the engine, `@jm/runtime` or `jm` didn't offer at first.
All are now resolved in the engine; the right column says how, and how this
project uses it. **High** blocked a feature or was a bug, **med** forced an
awkward workaround, **low** was a nice-to-have.

| # | Gap | Severity | Resolution |
|---|---|---|---|
| 1 | **Bug:** `Camera.setPosition` moved the view twice as far. | high | Fixed in `Camera2D`; `lib/view.ts` passes positions straight through. |
| 2 | No tile maps: ~1000 tile entities per area and script-side collision. | med | Engine `TileMapComponent`: `assets/maps/*.txt` + `dungeon.tileset.json` (per-area look through tileset `vars`); `TileBody` with corner sliding replaces `lib/body.ts`. |
| 3 | A script couldn't read another entity's params or data. | med | `entity.params` and `entity.data`; the hero publishes its room in its own data for the area. |
| 4 | No messaging between scripts. | med | `entity.send` / `onMessage`: the hero sends the hermit `"talk"`. Opened doors become floor in the map itself. |
| 5 | A trapped script was logged as "entity 0:1" with no script name or line. | med | Trap logs name the script path; failed assertions add message and line. |
| 6 | `Scene.transition` was silently ignored while one was running. | med | Requests wait for the running transition; the menu guard is gone. |
| 7 | Collision pairs needed a `VelocityComponent` on one side. | low | A body counts as moving once it has moved; the zero velocities are gone. |
| 8 | `Sprite.play()` restarted the animation; no way to read the current one. | low | `play` keeps a running animation; `restart` and `animation` added. `playOnce` is gone. |
| 9 | `Overrides` couldn't add tags to spawned entities. | low | `Overrides.tag()`. |

Worked well from the start: the HTML HUD (hearts swapped with `UI.setAttribute(src)`), typed dialog over a paused world (`runWhenPaused`, now the shared `@demos/common` Dialog), per-room spawning with `World.findAll`, short sound/prefab names, the sword as a short-lived collider entity.
