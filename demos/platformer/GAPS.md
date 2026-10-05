# Engine gaps found building Super Pip

What a platformer needed that the engine, `@jm/runtime` or `jm` didn't offer at first.
All are now resolved in the engine; the right column says how, and how this
project uses it. **High** blocked a feature or was a bug, **med** forced an
awkward workaround, **low** was a nice-to-have.

| # | Gap | Severity | Resolution |
|---|---|---|---|
| 1 | **Bug:** `Camera.setPosition(x, y)` showed (2x, 2y): `Camera2D` applied the position in both the projection and the view matrix. | high | Fixed; shakes doubled so they look as before. |
| 2 | No tile maps: no tile collision, and every tile was its own entity. | med | Engine `TileMapComponent`: `assets/maps/<level>.txt` + `pip.tileset.json` (edge rule for ground tops, lava frames, `deadly`/`bumpable` tags); `Body` extends `TileBody`. |
| 3 | Scripts couldn't share data; broken bricks went through `GameState` keys. | med | Smashing a brick sets its map tile open (`map.set`); every script reads the same map. |
| 4 | No messaging between scripts. | med | Messages: "stomp", "bump", "smash", "kick" (with a direction), "hit". |
| 5 | Collisions needed a `VelocityComponent` on one side. | low | A body counts as moving once it has moved; the zero velocities are gone. |
| 6 | No gravity or acceleration in physics. | low | `VelocityComponent.acceleration`: debris and popped coins fall without `falling.ts`. |
| 7 | `Sprite.play()` restarted the animation. | low | `play` keeps a running animation; `playOnce` is gone. |
| 8 | `Overrides` couldn't add tags. | low | `Overrides.tag()`: the level tags each block with its tile. |
| 9 | No world-space text (floating score popups). | low | `TextComponent`: stomps and kicks float their points. |

Worked well from the start: per-entity scripts with params, prefabs and overrides, SpriteAnimation, atlases from loose PNGs, HTML HUD/screens, scene flow, GameState/Save, pause via `runWhenPaused`, short asset names.
