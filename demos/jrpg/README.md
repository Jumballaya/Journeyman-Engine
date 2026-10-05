# Embers of Aldane

A slice of a Final Fantasy / Chrono Trigger style RPG, built on Journeyman
without engine changes:

- **Overworld:** the village of Aldane and the Emberwood, with talking villagers,
  treasure chests, an inn, a shop, save crystals, and a party menu (Esc).
- **Encounters:** random battles in tall grass, behind a swirl transition shader.
- **Battles:** active-time battles for a party of three (Kael, Lyra, Bram).
  - Commands: FIGHT / TECH / ITEM / DEFEND / RUN.
  - Elemental weaknesses, poison and sleep, and a dual tech (FLAME BLADE).
  - Damage numbers, then XP, levels and gold.
- **Finale:** the Cinder Wyrm boss in its lair, then the ending. Game over loads the last save.

All art and music are original and generated (`tools/gen_assets.py`) in a
16-bit style:
- shapes are painted as materials and shaded by `tools/pixelart.py` (light from the top
  left, dithered steps, coloured outlines);
- people are posed puppets (`tools/characters.py`);
- monsters come from `tools/monsters.py`;
- tiles come from `tools/terrain.py`, with 16 auto-tiled edge variants for paths and water;
- battle backdrops are layered and dithered;
- windows use a gradient texture.

```sh
cd assets/scripts && npm install && cd ../..   # once
jm build && jm run
node --test tests/*.test.mjs                    # battle and party rule tests (after one jm build)
python3 tools/gen_assets.py                     # regenerate art and sounds
```

**Controls:** arrows move, Z confirm / talk, X back, Esc party menu, F11 fullscreen.

## How it's made

| File | Role |
|---|---|
| `assets/scripts/lib/data.ts` | the content: heroes, skills, items, enemies, encounters |
| `assets/scripts/lib/battle.ts` | the battle rules: gauges, commands, damage, statuses, AI, rewards (tested) |
| `assets/scripts/lib/party.ts` | levels, HP/MP, gold, items, flags, position; save and load (tested) |
| `assets/scripts/lib/stage.ts` | how a battle looks: sprites, lunges, flashes, effects, damage numbers |
| `assets/scripts/battle.ts` | the battle scene: command menus, targeting, turn playback, results |
| `assets/scripts/lib/maps.ts`, `world.ts` | the maps as ASCII; building them, the camera, the party menu |
| `assets/scripts/hero.ts` | walking, talking, map exits, random encounters |
| `assets/scripts/{npc,inn,shop,chest,crystal,lair}.ts` | one script per kind of thing to talk to |
| `assets/shaders/swirl.frag` | the battle transition |

**Testing:** `scenes/test_boss.scene.json` starts the wyrm fight directly
(`JM_ENTRY_SCENE=scenes/test_boss.scene.json`); the battle scene takes an
`encounter` param.

See [GAPS.md](GAPS.md) for what the engine was missing.
