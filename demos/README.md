# Demos

Games built on Journeyman to test whether it makes real games simply,
**without engine or CLI changes**. Each project is self-contained (`jm init`,
own assets, tools and tests) and keeps a `GAPS.md` of what the engine lacked.

| Demo | Kind | Highlights |
|---|---|---|
| [strike_wing](strike_wing/) | vertical shmup | waves, boss, menus, CRT/transition shaders |
| [tetris](tetris/) | puzzle | guideline rules, auto-repeat, rule tests |
| [platformer](platformer/) | side-scroller | tile physics, stomps, power-ups, flagpole, boss |
| [dungeon](dungeon/) | top-down adventure | room scrolling, sword, keys/doors, dialog, boss |
| [jrpg](jrpg/) | RPG slice | maps, ATB battles, party, save/load, swirl transition |

Run one: `cd <demo>/assets/scripts && npm install && cd ../.. && jm build && jm run`.

## What the engine needs next (from the GAPS files)

1. **Bug: `Camera.setPosition` moves twice as far** (`Camera2D` applies the
   position in both projection and view). Three demos work around it in one
   `lib/view.ts` each.
2. **Tile maps:** tile collision and batched tile layers. Three demos rebuilt
   them in script (ASCII maps, `lib/body.ts`) with hundreds of tile entities.
3. **Sharing data between scripts:** reading another entity's params or data,
   messages between scripts, and richer GameState (lists, key enumeration).
   Today tags are used as mailboxes.
4. **Loading data files from scripts** (JSON/text), so content can live in
   data rather than code.
5. **World-space text** for damage numbers and score popups.
6. **Smaller items:**
   - a spawned entity's components aren't ready on its first frame;
   - colliders need a `VelocityComponent`;
   - `Sprite.play()` restarts, and there's no texture setter;
   - `Overrides` can't add tags;
   - trap logs don't name the script;
   - `Scene.transition` silently ignores requests during a transition;
   - no modifier keys or input repeat;
   - no shared script libraries across projects;
   - no `jm test`.
