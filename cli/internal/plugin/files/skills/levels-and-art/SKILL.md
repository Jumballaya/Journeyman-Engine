---
name: levels-and-art
description: Make and change levels, sprites and tile maps in a Journeyman game - scenes and prefabs, atlases and Aseprite sheets, Tiled maps. Use when the person asks for a new level, an enemy, a sprite, an animation or a map.
---

# Levels, sprites and maps

- **Levels are scenes.** `generate` (kind `scene`, a name) makes one and lists
  it in `.jm.json`; `settings_update` with `entryScene` makes it the one the
  game starts in. Entities are JSON with components: `schema` lists each
  component's keys and defaults. Show the result with `scene_view`.
- **Repeated things are prefabs** (`assets/prefabs/*.prefab.json`): a scene
  entity uses one with `"prefab"` plus its own overrides; scripts
  `spawn("name", x, y)` them. `generate` (kind `prefab`) starts one.
- **Sprites come from atlases.** An `*.atlas.json` lists PNGs (or Aseprite
  sheets exported with JSON data and tags); `build` packs them, and a sprite
  names a region as `assets/atlases/game.atlas.json#ship`. Aseprite tags
  become animation clips for `SpriteAnimationComponent`. Details:
  `jm docs content`, *Atlases & sprite animation*.
- **Maps are Tiled files** (`*.tmj`, `*.tsj`): a `TileMapComponent` draws
  one; tile classes and properties (`solid`, tags) are what scripts and
  bodies read; object layers of class `ground` or `platform` are ground.
  The person can paint them in Tiled or the editor.

When "this enemy" or "that level" is unclear, `pick` asks the person to point
at it. After a change, `build`, then look at it (`drive_frame`) before
saying it's done.
