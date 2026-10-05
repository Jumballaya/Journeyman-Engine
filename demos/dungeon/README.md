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
| `scenes/{grove,crypt}.scene.json` | one scene per area; its map prefab picks the area's look (tileset vars) |
| `assets/scripts/lib/areas.ts`, `lib/tiles.ts` | the areas' music and rooms |
| `assets/scripts/lib/foe.ts` | what enemies share: health, knockback, hit flash, drops |
| `@demos/common` (`../common`) | the typed dialog box over a paused world |
| `assets/scripts/lib/session.ts` | hearts, gems, keys, sword, and one-time world changes |
| `assets/scripts/area.ts` | spawns what the map marks, scrolls between rooms and spawns each room's enemies and items; HUD, music, pause |
| `assets/scripts/hero.ts` | Wren: movement (`TileBody` with corner sliding), sword, talking, doors, stairs, damage |
| `assets/scripts/{slime,bat,skull,boss}.ts` | the enemies |
| `assets/scripts/{item,door,hermit}.ts` | pickups (they apply themselves), locked doors, the hermit |

**Testing and level design:** the area scene takes warp params (`area`,
`tx`/`ty`, `sword`, `keys`, `hearts`); see `scenes/test_*.scene.json`, e.g.
`JM_ENTRY_SCENE=scenes/test_boss.scene.json`.

See [GAPS.md](GAPS.md) for what the engine was missing.
