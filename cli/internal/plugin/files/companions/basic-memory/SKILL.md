---
name: basic-memory
description: Remember a Journeyman game's design, decisions and playtest findings across chats with Basic Memory, over the same Markdown as the game's design/ folder. Use at the start of work on a game, and whenever something worth remembering is decided or found.
---

# Remembering the game with Basic Memory

Basic Memory keeps notes as Markdown and finds them again by meaning. Point
it at the game's `design/` folder so it remembers what the person sees:

- **Once per game:** a Basic Memory project for it, in its `design/` folder
  (`basic-memory project add <game> <game folder>/design`), and use that
  project for this game's notes.
- **Starting work:** search it for the game's pitch, the decisions made and
  the open questions before proposing anything.
- **As you go:** write a note when something is decided ("jump height is
  64px: the person wants it floaty"), when a play shows a problem (link the
  play and marker, `jm://play/<id>/m1`), and when it's fixed.
- The notes are plain files in `design/`: the person reads them, in Obsidian
  if they use it, and they go in the game's git history.
