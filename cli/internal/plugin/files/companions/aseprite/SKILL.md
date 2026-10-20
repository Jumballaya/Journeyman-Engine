---
name: aseprite
description: Draw and animate sprites in Aseprite for a Journeyman game, held to the game's art direction (design/Art.md), validated and critiqued, then brought in as an atlas with animation clips. Use when the person wants a new sprite, an animation, a tileset, or to change or review art made in Aseprite.
---

# Sprites with Aseprite

The game's look is decided once, in `design/Art.md` (a design note of type
`art`): its properties are the locks every picture keeps, its sections the
rest. Every sprite, whoever draws it, starts from it, so a hundred sprites
look like one game.

## Before drawing: the brief is Art.md

Read `design/Art.md` (`memory` with note `Art`) and the dossier you're
drawing. Art.md answers what a sprite brief asks; don't ask the person again:

| Property | What it fixes |
|---|---|
| `palette` | the palette file (`.gpl`, `.hex`, `.pal`): load it into the sprite; draw only with it |
| `grid` | tiles and props are multiples of it |
| `view`, `light` | the drawing's angle and where the light comes from |
| `outline` | the outline colour, by its name in the palette |
| `colors` | the most colours one frame uses |
| `sizes` | each dossier type's (or set's) frame: `character: 32x32` |
| `animations` | each set's directions and actions: tags `<action>_<direction>` (an undirected action by its name), each frame's milliseconds |
| `references` | finished sprites new ones match |

The dossier's type picks its size and set (`art_set` overrides). If Art.md
has no palette or grid yet, settle them with the person first (the
`art_direction` playbook) and write them in.

## Draw

With the Aseprite companion's tools (its MCP server), following its own
workflows (brief, draw, animate):

1. A new sprite at the frame size, the palette loaded from Art.md's file, as
   `assets/art/<kind>/<name>.aseprite`.
2. Import a reference (one of Art.md's `references`, the same set if one is)
   on a locked layer, to match its proportions, shading and outline.
3. Draw it; then the set's actions, a frame per entry, a tag per action and
   direction, each frame timed as the set says. Look at it after each step
   (preview, filmstrip for animations).

## Check, then critique

- **Validate** with the tools (`validate`: palette, strays, outlines,
  anti-aliasing, animation) and fix every error.
- **Critique** it: the `pixel-critic` agent where the agent has it, otherwise
  the tools' review workflow (`skill://review`). A score out of 10 against
  Art.md and the references; under 7, fix the highest-value finding and
  critique again.
- **Note the critique** on the dossier: `memory_note` (category `critique`):
  the score, and what's left. A rule the critic keeps citing becomes Art.md's
  (category `rule`), so the next sprite starts with it.

## Into the game

1. **Export a sheet with its data** beside the source: `assets/art/<kind>/<name>.png`
   + `.json` (the tools' sheet export, JSON data, tags on; or
   `aseprite -b <name>.aseprite --sheet <name>.png --data <name>.json --list-tags --format json-array`).
2. **Into the atlas**: list the `.json` in the game's `*.atlas.json`
   `sources`. `build` packs it: each frame is a region (`kage_0`, …), each
   tag a clip with its frames' own durations.
3. **The dossier**: `sprite: assets/art/<kind>/<name>.aseprite`, and
   `art: done` once the critique passes.
4. **Build.** `build` (and `health`, checks `art`) holds the export to Art.md:
   colours off the palette, half-clear pixels, frames of the wrong size,
   missing or mistimed tags, sheets no atlas lists, tiles off the grid. Its
   art warnings must be gone.
5. **Play it**: `"SpriteAnimationComponent": {"atlasPath": "assets/atlases/game.atlas.json", "sheet": "kage", "current": "idle_down"}`;
   scripts switch `current`. Look at it in the game (`drive_frame`).

Tilesets the same way: tiles on the grid, the palette, exported as Tiled
`.tsj` (the tools' tileset export) into `assets/maps/`.

Keep the `.aseprite` files: they're the source, the PNG and JSON are made from them.
