---
name: design-memory
description: Keep the game's design notes - read them before you design, record what the person decides and why. Use for design talk, a new mechanic or level, when the person says "remember", or asks "why did we".
---

# The game's design memory

The game's `design/` folder is what it is and why: Markdown notes in Basic
Memory's format (observations `- [feel] text`, relations `- serves
[[Pillars]]`) that open as an Obsidian vault. The person reads them there;
the next chat starts from them.

1. `settings_read` once: `agent.recordDecisions` false means note only what
   the person asks you to.
2. **Read before you design.** `memory` lists the notes (Pitch, Pillars,
   Levels, Systems, Decisions, Notes); `memory` with a `note` reads one with
   its links; `memory_search` finds a word in all of them. Answer "why did
   we" from Decisions, quoting the note.
3. **Note what you learn.** `memory_note` (`note`: Levels, Systems, Pitch...;
   `category`: feel, idea, bug, playtest) for what the person says about how
   it feels and what a play showed.
4. **Record decisions.** Once you settle something together,
   `memory_decide` with the `title` (what was decided) and `why`.
5. **The world goes in dossiers.** A character, item, location, faction,
   quest or piece of lore is a note of its own: `dossier_new` (`kind`,
   `name`, `summary`, `props`), in `design/<Folder>/` from the kind's
   template. `memory` lists the kinds and their properties; `memory` with a
   `kind` lists that kind's dossiers. Before writing one, read what it
   relates to (`memory` with `note`) so the new one agrees with them, and
   link them: props like `{"home": "[[Irongate]]"}`, `[[links]]` in its text.
   A kind of the game's own is a template file in `design/_templates/`.
6. **Link it.** `[[Note]]` links notes; name scene files in backticks
   (`scenes/level1.scene.json`), entities as
   `jm://entity?scene=…&name=Hero`, plays and markers as `jm://play/<id>/m1`.
   One subject per note.

Show a note with `studio` and route `/memory?note=<title>`.
