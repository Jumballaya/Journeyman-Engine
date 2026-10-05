# Engine gaps found building Embers of Aldane

What a JRPG slice needed that the engine or `@jm/runtime` doesn't offer, and
how this project works around it. **High** blocks a feature or is a bug,
**med** forces an awkward workaround, **low** is a nice-to-have.

| # | Gap | Severity | Workaround here |
|---|---|---|---|
| 1 | **Bug:** `Camera.setPosition` moves the view twice as far (see demos/platformer/GAPS.md #1). | high | `lib/view.ts` halves the position, marked TODO. |
| 2 | Scripts can't load data files (JSON or text assets), so content can't live in data files. | med | Party, skills, items, enemies and encounters are typed TS modules (`lib/data.ts`, `lib/maps.ts`). |
| 3 | No world-space text. | med | Damage numbers are absolutely positioned HTML (`#pop-*`), converting world to UI pixels in `lib/stage.ts`. |
| 4 | `GameState`/`Save` hold only numbers and strings, and keys can't be listed. | med | `lib/party.ts` copies a known key list to save; story flags are one comma-joined string. |
| 5 | Script code can't be shared between projects. | low | `lib/body.ts` and `lib/dialog.ts` are copies from demos/dungeon. |
| 6 | A sprite's texture can't be set from a script. | low | The chest prefab has `closed`/`open` animations and plays one. |
| 7 | `@jm/runtime` only appears in `node_modules` after the first `jm build`; script tests fail before that. | low | Run `jm build` once first (README). |
| 8 | Scripts can't read other entities' params (as in the other demos). | low | Every interactable runs its own dialog; the hero just tags it `talk`. |

Worked well: the custom transition shader (`swirl.frag`) through
`Scene.transition(scene, s, "swirl")`; state surviving scene changes in
GameState (map ↔ battle round trips); HTML windows, menus and ATB bars;
`runWhenPaused` dialogs and menus; Node tests of the battle rules.
