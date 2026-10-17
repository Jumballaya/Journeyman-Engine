---
name: obsidian
description: Keep a Journeyman game's design notes in Obsidian - the game's design/ folder is the vault - and link notes to scenes, entities and plays. Use when the person talks about the game's design, ideas, levels to make, or their notes.
---

# Design notes in Obsidian

The game's `design/` folder is an Obsidian vault: plain Markdown files the
person reads and writes in Obsidian, and you read and write as files.

- **Read before designing.** Look in `design/` for the pitch, the pillars and
  what was decided before you suggest or build something new.
- **Write it down.** A decision, a level idea, what a playtest showed: add or
  update a note (one subject per note, `[[links]]` between them). Name things
  as the game does: scene files (`scenes/level1.scene.json`), entities
  (`scenes/level1.scene.json#Hero`), plays and markers (`jm://play/<id>/m1`).
- **Show the person:** `obsidian://open?path=<absolute path of the note>`
  opens the note in Obsidian.
- The person sees your notes in Obsidian as soon as you write them; their
  edits there are files you read the same way.
