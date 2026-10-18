---
name: tasks
description: The game's task board - plan the work, link each task to what it's about, move it as it goes, report what's done. Use for planning, "what's next", "later", and after a playtest that turned up several issues.
---

# The task board

The board is `design/Tasks.md`: To do, Doing, Done, a task per line with an
id (`t3`). The person sees it in Obsidian and Studio's Tasks tab.

1. `settings_read` once and follow the `agent.*` preferences as you work
   (commits, experiments, decisions).
2. `tasks` first: pick up what the person asked for earlier before starting
   something new. "What's next" is the top of To do.
3. **One task per issue.** After a playtest with several problems,
   `task_add` each with `text` and `links`: the marker
   (`jm://play/<id>/m2`), the entity (`jm://entity?scene=…&name=…`), the
   file, the design note (`[[Levels]]`).
4. **Move it as work goes.** `task_move` with the `id` to `column` Doing
   when you start it, Done once it's built, played and the person agrees.
   `task_remove` only what's no longer wanted.
5. **Report.** Say what moved to Done and what's left, by task text.

Show the board with `studio` and route `/tasks`.
