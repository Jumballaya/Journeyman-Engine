---
name: onboarding
description: Set up Journeyman after it's installed - see the person's games, set the editor's preferences, then make a first game or open one in Journeyman Studio. Use when the person chooses Set up for Journeyman or asks to get started with it.
---

# Set up Journeyman

Journeyman is a 2D game engine the person and you work on together: they
play the game in its own window and in the editor, you change its files, and
Journeyman's pages here show what you both mean. Keep setup short: a few
questions, one at a time, then show them something real.

1. Call `games` with `{}`. It lists their games (newest opened first) and the
   one open now. Say in a line what you found.
2. Call `settings_read` with `{}`. Ask, one at a time, using what they've
   already said:
   - "Should the editor follow your system's light/dark, or always be light or
     dark?" (`editor.appearance`: system, light, dark)
   - "Which app should open scripts?" (`editor.codeEditor`: vscode, cursor,
     zed, sublime, text, or ask)
   - How you'll work, said from the Agent values `settings_read` gave (by
     default: commit a version after each change that works, try risky
     changes in a sandbox, write what we decide into the design notes, ask
     before going back to an older version), then "Change any?"
     (`agent.commitMilestones`, `agent.experiments`: sandbox, variant or ask;
     `agent.recordDecisions`, `agent.askBeforeRestore`)
   Then call `settings_update` with only what they changed, e.g.
   `{"set": {"editor.appearance": "dark"}}`. Confirm from the values it
   returns; if it fails, say so. If nothing changed, skip it.
3. No games yet: offer a first one. Call `new_game` with `{}`: where the host
   shows forms, the person picks an example or an empty game, its name and its
   picture's shape there. Elsewhere ask for a name and call
   `new_game {"name": …}`. Games already: ask which one to work on and
   `open_game` it.
4. Call `studio` with `{"route": "/game"}` to show them the game: its scenes,
   its plays, and buttons to open it in the editor or play it.
5. Tell them how you work together, in two sentences: they play it
   (`project_play` opens it in its own window; every play is recorded, and F8
   marks a moment), then tell you how it felt, and you look at that play
   (`play_show`) before changing anything.

If they installed Journeyman in the middle of a task, go back to that task in
the same chat once setup is done.
