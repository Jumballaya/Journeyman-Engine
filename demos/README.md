# Demos

Games built on Journeyman to test whether it makes real games simply. Each
project is self-contained (`jm init`, own assets, tools and tests), apart from
script code shared through `common/`, and keeps a `GAPS.md` of what the engine
lacked when it was written and how that was resolved.

| Demo | Kind | Highlights |
|---|---|---|
| [strike_wing](strike_wing/) | vertical shmup | waves, boss, menus, CRT/transition shaders |
| [tetris](tetris/) | puzzle | guideline rules, auto-repeat, rule tests |
| [platformer](platformer/) | side-scroller | tile physics, stomps, power-ups, flagpole, boss |
| [dungeon](dungeon/) | top-down adventure | room scrolling, sword, keys/doors, dialog, boss |
| [jrpg](jrpg/) | RPG slice | maps, ATB battles, party, save/load, swirl transition |
| [pellet_party](pellet_party/) | 2–4 player party game, peer to peer | owner-simulated players, host-run pellets, scores in the session store, host migration |
| [tank_arena](tank_arena/) | up to 8 players on a dedicated server | server-simulated tanks on players' input, server-only rules and bots (`net.server.scripts`) |
| [checkers](checkers/) | online checkers: a matchmaker server, then peer to peer | a dedicated matchmaker from the same files, NAT punching, a shared board judged by the host, rule tests |
| [Ash and Iron](<Ash and Iron/>) | turn-based RPG slice, built in the editor | grid combat with AP, mouse and keys, quests and dialogue from data tables, three save slots |
| [neon_vow](neon_vow/) | future-samurai platformer, built agent-first | painted neon ruins, cybernetic samurai, cable swings, shrine checkpoints, drawn ground and parallax |

Run one: `cd <demo>/assets/scripts && npm install && cd ../.. && jm build && jm run`.
Test one (tetris, jrpg, checkers): `jm test`. Play a multiplayer one (pellet_party,
tank_arena, checkers) on one machine: `jm run --peers 2`.

## What the demos taught the engine (from the GAPS files)

Each demo first worked around what the engine lacked; all of it is now in the
engine, runtime or CLI, and the demos use it:

1. **Camera:** `Camera.setPosition` moved the view twice as far (fixed).
2. **Tile maps:** `TileMapComponent` drawing Tiled maps and tilesets (layers,
   terrains, animated and tagged tiles, objects), without per-tile entities;
   `TileMap` / `TileBody` for queries and collision. Used by the platformer,
   dungeon, JRPG and Ash and Iron, whose maps are `assets/maps/*.tmj`.
3. **Sharing data between scripts:** messages (`entity.send`, `onMessage`),
   other entities' params (`entity.params`) and data (`entity.data`), and
   stores holding lists and JSON with key listing. No more tags as mailboxes.
4. **Data files:** `Data.json` / `Data.text` (the JRPG's bestiary).
5. **World-space text:** `TextComponent` (damage numbers, score popups).
6. **Smaller items:** writes to just-spawned entities apply once they exist;
   colliders don't need a `VelocityComponent`; velocity acceleration (gravity);
   `Sprite.play()` keeps a running animation (`restart`, `animation`,
   `setTexture`); `Overrides.tag()`; trap logs name the script; transition
   requests wait instead of being dropped; modifier keys and
   `Input.repeated`; `UI.worldRect` and camera conversions; asset globs in
   `.jm.json`; shared script libraries (`scriptLibraries`, see `common/`);
   `jm test`.
