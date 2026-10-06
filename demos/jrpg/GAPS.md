# Engine gaps found building Embers of Aldane

What a JRPG slice needed that the engine, `@jm/runtime` or `jm` didn't offer at first.
All are now resolved in the engine; the right column says how, and how this
project uses it. **High** blocked a feature or was a bug, **med** forced an
awkward workaround, **low** was a nice-to-have.

| # | Gap | Severity | Resolution |
|---|---|---|---|
| 1 | **Bug:** `Camera.setPosition` moved the view twice as far. | high | Fixed in `Camera2D`; `lib/view.ts` passes positions straight through. |
| 2 | Scripts couldn't load data files. | med | `Data.json`/`Data.text`: enemies and encounters live in `assets/data/bestiary.json`, maps in `assets/maps/*.txt`. |
| 3 | No world-space text. | med | `TextComponent`: damage numbers are `popup` entities instead of positioned HTML. |
| 4 | `GameState`/`Save` held only numbers and strings, and keys couldn't be listed. | med | Stores hold any JSON (`getStrings`, `getJson`...) and list `keys()`: flags are a list, saves copy every key. |
| 5 | Script code couldn't be shared between projects. | low | `scriptLibraries` in `.jm.json`: `Dialog` lives in `demos/common` (`@demos/common`). |
| 6 | A sprite's texture couldn't be set from a script. | low | `Sprite.setTexture()` (the chest still uses its two animations). |
| 7 | `@jm/runtime` only appeared after the first `jm build`; script tests failed before that. | low | `jm test` extracts it and runs `tests/*.spec.ts` itself. |
| 8 | Scripts couldn't read other entities' params. | low | `entity.params`; talking is now a `"talk"` message. |
| 9 | No auto-tiling or tile layers. | low | Tileset `{mask}` images with `joins`, `under` tiles (people on the road, a bridge over water) and animated `{frame}`s: `aldane.tileset.json`. |
| 10 | New prefabs had to be added to `.jm.json` by hand. | low | Asset globs (`"assets/prefabs/*.prefab.json"`), expanded by `jm build`. |

Worked well from the start: the custom transition shader (`swirl.frag`) through `Scene.transition(scene, s, "swirl")`; state surviving scene changes in GameState (map ↔ battle round trips); HTML windows, menus and ATB bars; `runWhenPaused` dialogs and menus; script tests of the battle rules.
