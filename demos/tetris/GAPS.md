# Engine gaps found building Tetris

What this game needed that the engine or `@jm/runtime` doesn't offer, and how
the project works around it. **High** blocks a feature, **med** forces an
awkward workaround, **low** is a nice-to-have.

| # | Gap | Severity | Workaround here |
|---|---|---|---|
| 1 | No modifier keys (Shift, Ctrl, Alt) in `Key` / bindings. Guideline Tetris holds with Shift. | med | Hold is `C` and the bumpers. |
| 2 | A spawned entity's components appear next frame; writes to it before then are silently dropped, with no "ready" signal. | med | `lib/well.ts` polls `cells[0].has("SpriteComponent")` before drawing. |
| 3 | World sprites can't be positioned relative to UI layout (or UI can't host sprites). The well's frame lives in HTML, its cells in world space. | low | Matching constants in `game.ui.html` and `lib/well.ts`, cross-referenced in comments. |
| 4 | No input auto-repeat (DAS/ARR) in `Input`. | low | `lib/autorepeat.ts`. |
| 5 | No way to unit-test project scripts (`jm test`). Importing `@jm/runtime` also links engine host functions into pure-logic modules. | low | `tests/game.test.mjs` compiles the spec with the project's `asc` and stubs every host import. |

Not gaps: everything else (rules, menus, pause, previews, saving the high
score, music and effects) was straightforward with the existing API.
