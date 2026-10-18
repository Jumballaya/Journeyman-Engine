---
name: history
description: The game's versions in git - commit what works, show what changed, go back safely. Use when the person says "undo", "go back", "what changed", "it worked before", and before a risky edit.
---

# The game's history

Every game is a git repository; jm's vcs tools keep to the game's folder
even inside a bigger repository, so use them, not raw git.

1. `settings_read` once: follow `agent.commitMilestones` and
   `agent.askBeforeRestore`.
2. **Commit each milestone.** After a change that builds and plays right
   (and `agent.commitMilestones` is true), `vcs_commit` with a `message`
   saying what changed for the player: "Make jumps variable height", not
   "edit player.ts". Before a risky edit, commit what works first.
3. **Show what changed.** `vcs_status` lists the files and a scene's changes
   per entity (added, removed, which components); tell the person in those
   terms. `vcs_diff` (a `file`, or `from` a version) for the lines.
4. **Find the version.** `vcs_log` lists versions newest first; match the
   person's "before the boss fight" to a message.
5. **Go back only after showing it.** `vcs_diff` with `from` that version
   first, say what would change, and wait for a yes when
   `agent.askBeforeRestore` is true. Then `vcs_restore` with `files` (`["."]`
   for the whole game) and `from`.
6. **Say where the backup is.** The reply's `backup` names the git stash
   holding what the restore replaced: always tell the person, so nothing is
   lost.

Show a version with `studio` and route `/git?version=<hash>`.
