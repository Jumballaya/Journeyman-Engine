# Ash and Iron

A turn-based, top-down RPG slice in the spirit of the old isometric Fallouts,
made in the Journeyman editor: maps painted with its tile tools, people and
things placed from prefabs, every item, ability, enemy, quest and line of
dialogue entered in its data editor, screens built in its UI editor. Scripts
and art are the only things made outside it.

**The slice:** a title screen with three save slots, an intro, the town of
Cinderwell (five people, three quests, a shop, a scrapyard), the Ashen Road
(a raider camp, a drone) and the Foundry, where the Iron Warden waits.

**Playing:** click to walk, talk, search and fight, or use the keys (arrows /
WASD to move, E or Enter to act, I/J/C for items, quests and character, Esc
for the menu). In a fight you have action points: a step costs one, an
ability its cost. Hover a tile to see the walk and its cost, click a foe to
use the armed ability (click a slot or press 1-4 to arm one), and End Turn
(Space) when you're done. Slag burns.

**Run it:** open the folder in the editor and Play, or
`cd assets/scripts && npm install && cd ../.. && jm build && jm run`.

## Where things are

- `assets/data/`: items, abilities, enemies, people, dialogue (lines, choices,
  who opens with what), quests and their stages, levels, the shop.
- `scenes/`: title, intro, cinderwell, ashen_road, foundry, ending.
- `assets/scripts/game.ts`: exploring, talking, menus, the shop and fights, run
  by each map scene's `hud` entity; `lib/state.ts` is the story so far (and
  the conditions and actions the dialogue tables use), `lib/slots.ts` the save
  slots, `lib/grid.ts` paths and sight, `lib/pointer.ts` the mouse.
- `tools/make_art.py`: the art and sound, drawn from code into `tools/out/`
  and imported through the editor. People are Antifarea's (see CREDITS.md).
