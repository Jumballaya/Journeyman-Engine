# Art direction: architecture for review

How a game's look is written down once, and how every agent that draws for it
(with the Aseprite MCP or anything else) is held to it. This page is for
critique: it says what each piece does, why, and what's still open.

## The problem

An agent drawing sprites starts each one from nothing: it asks (or guesses)
the palette, the size, the view, the outline, the animation names. Two
sprites drawn a day apart don't match, and nothing notices until someone
looks. The Aseprite MCP already gives any agent the drawing, `validate` and a
critic; what's missing is the game's side: what "matching" means for this
game, and a check that the files the game ships actually match.

Constraints we agreed on:

- No `art` command. The agent draws with the tools it has (the Aseprite MCP);
  jm only says what the art must be and checks what came out.
- No Python helpers: everything is Go in jm, or the Aseprite MCP.
- Any agent (Codex, Claude, ChatGPT) gets it the same way: through files,
  `jm build` and the `health` tool.

## The pieces

```
design/Art.md          the locks (a memory note of type art)
design/Characters/*.md the sprites to draw (dossiers with sprite: …)
assets/**.aseprite     what the agent draws (Aseprite MCP)
assets/**.json/.png    what Aseprite exports (sheet + picture)
*.atlas.json, tilesets what the game ships

internal/art           reads Art.md, checks the exports against it
jm build               prints its findings as warnings (category art)
health "art" check     the same findings, plus each sprite's status
```

### 1. `design/Art.md`: the locks

A design note like any other in the memory vault (Obsidian-compatible
markdown), with `type: art`. `jm init` and `jm memory init` write it with
empty properties. Its YAML properties are the locks:

| property | what it locks | checked by jm |
|---|---|---|
| `palette` | a `.gpl`, `.hex` or `.pal` file in the game | every exported picture uses only its colours (or is clear) |
| `grid` | tile size | tilesets' tiles are multiples of it |
| `view`, `light` | top-down / side, light direction | no: for the agent and the critic |
| `outline` | a palette colour's name | that it is a palette colour |
| `sizes` | frame size per dossier type or set (`character: 32x32`) | each sheet's frames are that size |
| `animations` | per set: directions, each action's frame timings | each sheet has every `<action>_<direction>` tag, with those timings |
| `references` | finished sprites new ones should match | that the files exist; matching is the critic's |

The body (Palette ramps, Shading, Visual language) is prose for whoever draws.

Example:

```yaml
palette: art/endesga-32.gpl
grid: 16
view: top-down
light: top-left
outline: ink
sizes:
  character: 32x32
animations:
  character:
    directions: [down, up, left]
    idle: [400, 400]
    walk: 4          # four frames, any timing
```

That wants tags `idle_down`, `idle_up`, `idle_left`, `walk_down`, … on every
character's sheet.

### 2. Dossiers: what to draw

A Character (or the new Creature kind) dossier gets two properties:

- `sprite: assets/sprites/knight.aseprite`: where its art lives.
- `art: done`: the person (or agent) says it's finished.

`art_set` picks an animation set other than its type (one the note lacks
leaves it an asset of its type). A dossier is an
**asset** when it has a `sprite`, or its type is one `sizes`/`animations`
names. Each asset has a status, worked out from files, never stored:

| status | means |
|---|---|
| planned | no sprite file yet |
| drawn | the `.aseprite` exists, no sheet (`.json`) exported beside it |
| exported | the sheet exists |
| done | `art: done` and the checks find nothing wrong |

### 3. `internal/art`: the checks

`art.Check(root)` returns a report: the art note, the palette, every asset
with its status, and findings (file + message). It reads only what the game
ships (its manifest's assets, as regular files inside the game), and reports
a file's findings once even when an atlas and a dossier share it (it may read
the file twice). It checks:

- every atlas's pictures and every tileset's pictures: palette colours only;
- tilesets: tile size on the grid (a collection's tiles each, by their crop
  or picture);
- each asset's sheet: frame size, tags and timings for its set, its frames
  inside its picture, and an atlas lists it (or the game can't show it);
- the note itself: palette readable, outline in it, references present;
- `art: done` with no sprite or no sheet.

Everything is a **warning**: the game still builds.

What it does **not** check (left to the Aseprite MCP's `validate` and its
pixel-critic): colours per frame, readability, silhouette, shading,
consistency with the references.

### 4. Where it shows up

- `jm build` prints the findings (category `art`) with the other diagnostics,
  so every agent sees them after every build without being told to look.
- The MCP `health` tool has an `art` check: the findings plus "3 of 7 sprites
  done", with each sprite's status. Studio's Health tab doesn't run it yet.

### 5. The agent's loop (next PR)

The Aseprite companion skill (installed with `jm companions enable aseprite`)
teaches the loop; the onboarding interview asks about the look and fills
`Art.md`:

1. Read `design/Art.md` and the dossier.
2. Draw in Aseprite with the MCP, on the note's palette and size.
3. Tag the animations `<action>_<direction>`, set timings, export the sheet.
4. Run the MCP's `validate` and its pixel-critic.
5. `jm build`: fix the `art` warnings; add `art: done` when it's clean.

## Decisions to critique

1. **The locks live in a memory note**, not `.jm.json` or a new file format.
   For: the person edits it in Obsidian or Studio's Memory tab, agents already
   read the vault, prose and locks sit together. Against: YAML in a markdown
   front matter is easy to break (a parse error is an error finding).
2. **Warnings, not errors.** Art in progress shouldn't stop a build. Against:
   warnings get ignored. Alternative: errors once a sprite says `art: done`.
3. **Status is computed, only `done` is stated.** No state to go stale.
   Against: "drawn" vs "exported" is a file-naming convention (the sheet is the
   sprite's name with `.json`).
4. **Sets keyed by dossier type** (`character`, `creature`), `art_set` to
   override. Alternative: a set per dossier.
5. **Mechanical checks in jm, taste in the critic.** jm never judges how a
   sprite looks.
6. **Creatures** is a new dossier kind, because monsters were going into
   Characters.

## Open questions

- Should `jm build --strict` (or CI) treat art warnings as errors?
- Studio: run the art check in the Health tab, or give it an Art tab (the
  sprites board, planned → done, with thumbnails)?
- Palette formats: is `.gpl`/`.hex`/`.pal` enough, or read the palette out of
  an `.aseprite` file too?
