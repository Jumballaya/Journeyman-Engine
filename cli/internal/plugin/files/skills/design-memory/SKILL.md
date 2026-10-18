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
2. **A new game is planned first.** When `open_game` says its design isn't
   planned, or the person starts a new one, call `onboard`. Ask the step it
   gives, one question at a time with a suggestion. Keep the answer the way
   the step says, then call `onboard` again until it says the game is
   planned. If a step doesn't apply, `onboard` with `skip`.
3. **Read before you design.** `memory` lists the notes (Pitch, Pillars,
   Levels, Systems, Decisions, Notes); `memory` with a `note` reads one with
   its links; `memory_search` finds a word in all of them. Answer "why did
   we" from Decisions, quoting the note.
4. **Note what you learn.** `memory_note` (`note`: Levels, Systems, Pitch...;
   `category`: feel, idea, bug, playtest) for what the person says about how
   it feels and what a play showed.
5. **Record decisions.** Once you settle something together,
   `memory_decide` with the `title` (what was decided) and `why`.
6. **The world goes in dossiers.** A character, item, location, faction,
   quest or piece of lore is a note of its own: `dossier_new` (`kind`,
   `name`, `summary`, `props`), in `design/<Folder>/` from the kind's
   template. `memory` lists the kinds and their properties; `memory` with a
   `kind` lists that kind's dossiers. Before writing one, read what it
   relates to (`memory` with `note`) so the new one agrees with them, and
   link them: props like `{"home": "[[Irongate]]"}`, `[[links]]` in its text.
   A kind of the game's own is a template file in `design/_templates/`.
   For a world with places, peoples and families, look around first:
   `world_context` on what the new thing belongs to (its region, nation,
   family) gives what's there, how it connects and what's linked but
   unwritten; `world_path` how two things connect. State how it connects
   with `world_link` (a type like located_in, capital_of, member_of, or
   road_to with a distance in its details), once: the other side reads it. If it says the
   world now disagrees with itself, settle it with the person.
7. **Keep facts in mind.** When the person says "remember", or you settle
   canon, a rule or a constraint that every chat should know, `remember`
   it, one fact a call (`category` canon, rule, constraint, tried;
   `scope` you for the person's preference in every game). Opening the
   game reads them back. If it lists similar facts, `forget` the wrong one
   or `remember` with `replaces` (its id). `recall` finds them.
8. **Grow it by playbook.** For "add to the world", "make some
   characters", "make this place a level", "outline the story", "does this
   fit", or "draw it", call `playbook` (no name to list them, then with
   its `name` and `args`) and follow its steps.
9. **Link it.** `[[Note]]` links notes; name scene files in backticks
   (`scenes/level1.scene.json`), entities as
   `jm://entity?scene=…&name=Hero`, plays and markers as `jm://play/<id>/m1`.
   One subject per note.

Show a note with `studio` and route `/memory?note=<title>`.
