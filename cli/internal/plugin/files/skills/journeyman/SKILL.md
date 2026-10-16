---
name: journeyman
description: Work on a Journeyman 2D game with the person - find the game, read its guide, change its files and check the change. Use whenever the person talks about their game, a level, a sprite, a script or how it plays, or a folder has a .jm.json.
---

# Working on a Journeyman game

A Journeyman game is files: `.jm.json` (the manifest), `scenes/*.scene.json`,
`assets/prefabs/*.prefab.json`, AssemblyScript in `assets/scripts/*.ts`,
HTML/CSS UI in `assets/ui/`. The person works on the same files in the
Journeyman editor; you change them here.

1. **Which game.** No game open? Call `games`: it lists the person's games
   and the open one. Then `open_game` it (or `new_game`). Every other tool
   works on the open game.
2. **Its guide.** Read the game's `AGENTS.md` first: the files, the build,
   the driver, AssemblyScript's gotchas. `schema` lists every component's
   keys.
3. **Show, don't guess.** When it's unclear what they mean, `pick` asks them
   to point at the scene, prefab or texture; `scene_view` shows a scene laid
   out; `studio` shows the game. What they pick on these pages comes to you
   as a titled item.
4. **Change, then check.** Edit the files, `build`, fix every error it
   lists, then see it: `drive_start` (`gl: true`), `drive`, `drive_frame`,
   `drive_stop`. `test` runs logic tests; `golden` compares frames.
5. **Back to the person.** Say what changed in a sentence, and offer
   `project_open_editor` or `project_play` so they see it themselves.

Their plays are the best evidence of how the game feels: use the playtest
skill when they talk about playing.
