# Engine gaps found building Super Pip

What a platformer needed that the engine or `@jm/runtime` doesn't offer, and
how this project works around it. **High** blocks a feature or is a bug,
**med** forces an awkward workaround, **low** is a nice-to-have.

| # | Gap | Severity | Workaround here |
|---|---|---|---|
| 1 | **Bug:** `Camera.setPosition(x, y)` shows (2x, 2y). `Camera2D` applies the position in both the projection and the view matrix. Unnoticed before because Strike Wing only shakes. | high | `lib/view.ts` halves the position, marked TODO. One place to remove once fixed. |
| 2 | No tile maps: no tile collision, and every tile is its own entity. | med | ASCII maps in `lib/levels.ts`; `lib/body.ts` resolves boxes against the grid; `level.ts` spawns ~600 tile sprites. |
| 3 | Scripts can't share data. Every enemy keeps its own copy of the map, and changes (broken bricks) go through `GameState` keys. | med | `Session.tileKey()` flags in GameState; `TileMap.solid()` checks them for bricks. |
| 4 | No messaging between scripts. | med | Tags as mailboxes: Pip tags an enemy `stomped`, a block `bumped`; the receiver polls `hasTag` every frame. |
| 5 | Collisions are only reported when one side has a `VelocityComponent`, even for script-moved bodies. | low | Every actor prefab carries a zero `VelocityComponent`. |
| 6 | No gravity or acceleration in physics (only constant velocity). | low | `falling.ts` adds gravity to debris and popped coins. |
| 7 | `Sprite.play()` restarts the animation, and there's no way to read the current one. | low | `playOnce()` in `pip.ts` remembers what it last played. |
| 8 | `Overrides` can't add tags, so spawned entities can't be named. | low | Blocks tag themselves from their `tx`/`ty` params; the castle is found by a map convention (4 tiles right of the flagpole). |
| 9 | No world-space text (floating score popups). | low | Skipped. |

Worked well: per-entity scripts with params, prefabs and overrides,
SpriteAnimation, atlases from loose PNGs, HTML HUD/screens, scene flow,
GameState/Save, pause via `runWhenPaused`, short asset names.
