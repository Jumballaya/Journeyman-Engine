# The Legend of Hollow Grove

A top-down action adventure in the spirit of the first Zelda, built on
Journeyman without engine changes: a four-room overworld and a six-room
crypt with screen-by-screen scrolling, a hermit who hands you the sword,
slimes, bats and skeletons, keys and locked doors, heart containers, gems,
and Ogloth the boss guarding the shard. All art and music are original
(`tools/gen_assets.py`).

```sh
cd assets/scripts && npm install && cd ../..   # once
jm build && jm run
python3 tools/gen_assets.py                     # regenerate art and sounds
```

**Controls:** arrows move, Z sword / talk, Esc pause, F11 fullscreen.

## How it's made

| File | Role |
|---|---|
| `assets/maps/{grove,crypt}.txt`, `dungeon.tileset.json` | the two areas as ASCII, drawn by the engine's tile map (legend in `lib/areas.ts`) |
| `scenes/{grove,crypt}.scene.json` | one scene per area, authored: its map (the prefab picks the look), the hero, fires, hermit, doors, and each room's enemies and items in a group named `room-<x>-<y>`; taken items and opened doors carry an `unless` condition so they stay gone |
| `assets/scripts/lib/areas.ts`, `lib/tiles.ts` | the areas' music and rooms |
| `assets/scripts/lib/foe.ts` | what enemies share: health, knockback, hit flash, drops |
| `@demos/common` (`../common`) | the typed dialog box over a paused world |
| `assets/scripts/lib/session.ts` | hearts, gems, keys, sword, and one-time world changes |
| `assets/scripts/area.ts` | scrolls between rooms and swaps in each room's group; HUD, music, pause |
| `assets/scripts/hero.ts` | Wren: movement (`TileBody` with corner sliding), sword, talking, doors, stairs, damage |
| `assets/scripts/{slime,bat,skull,boss}.ts` | the enemies |
| `assets/scripts/{item,door,hermit}.ts` | pickups (they apply themselves), locked doors, the hermit |

**Testing and level design:** `scenes/test_*.scene.json` run `warp.ts`, which
starts a game from params (`area`, `tx`/`ty`, `sword`, `keys`, `hearts`) and
loads the real area scene, e.g.
`JM_ENTRY_SCENE=scenes/test_boss.scene.json`.

See [GAPS.md](GAPS.md) for what the engine was missing.
