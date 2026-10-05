# Engine gaps found building Tetris

What this game needed that the engine, `@jm/runtime` or `jm` didn't offer at first.
All are now resolved in the engine; the right column says how, and how this
project uses it. **High** blocked a feature or was a bug, **med** forced an
awkward workaround, **low** was a nice-to-have.

| # | Gap | Severity | Resolution |
|---|---|---|---|
| 1 | No modifier keys (Shift, Ctrl, Alt) in `Key` / bindings. | med | Left/Right Shift, Ctrl, Alt, Super keys; `"Shift"` binds both. Hold is Shift, C or the bumpers. |
| 2 | A spawned entity's components appeared next frame; writes before then were dropped. | med | Writes to a just-spawned entity apply once it exists: `lib/well.ts` no longer polls. |
| 3 | World sprites couldn't be positioned relative to the UI layout. | low | `UI.worldRect("well-cells")`: the well lays its cells over the HTML element. |
| 4 | No input auto-repeat (DAS/ARR). | low | `Input.repeated(action, delay, interval)`; `lib/autorepeat.ts` is gone. |
| 5 | No way to unit-test project scripts (`jm test`). | low | `jm test` runs `tests/game.spec.ts`; the hand-written Node harness is gone. |

Not gaps: everything else (rules, menus, pause, previews, saving the high score, music and effects).
