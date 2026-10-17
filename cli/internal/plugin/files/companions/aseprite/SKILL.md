---
name: aseprite
description: Draw and animate sprites in Aseprite for a Journeyman game, then bring them in as an atlas with animation clips. Use when the person wants a new sprite, an animation, or to change art they made in Aseprite.
---

# Sprites with Aseprite

The person draws in Aseprite; the game reads Aseprite's sprite-sheet export.

1. **Draw or change it.** With the `aseprite` tools (the companion's MCP
   server) make the sprite in the game's `art/` folder (`art/kage.aseprite`):
   one layer set, frames per animation, a tag per animation (`idle`, `run`,
   `attack`). Check your work with its preview tools before going on. Or ask
   the person to draw it; open it for them in Aseprite.
2. **Export a sheet with its data.** Aseprite's sprite-sheet export, with JSON
   data and tags on, to `art/kage.png` + `art/kage.json` (the tools' sheet
   export, or `aseprite -b art/kage.aseprite --sheet art/kage.png --data
   art/kage.json --list-tags --format json-array`).
3. **Into the atlas.** List `art/kage.json` in the game's `*.atlas.json`
   `sources`. `build` packs it: each frame is a region (`kage_0`, …) and each
   tag a clip, with its frames' own durations.
4. **Play it.** `"SpriteAnimationComponent": {"atlasPath": "assets/atlases/game.atlas.json", "sheet": "kage", "current": "idle"}`;
   scripts switch `current`. `drive_frame` to see it moving.

Keep the `.aseprite` files: they're the source, the PNG and JSON are made from them.
