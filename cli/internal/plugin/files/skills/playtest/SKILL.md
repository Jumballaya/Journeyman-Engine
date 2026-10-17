---
name: playtest
description: The play-and-fix loop - look at the person's recorded play before changing anything, then let them try the fix from the same spot. Use when the person mentions playing, a death, a bug, a marker, "just now", or how the game feels (floaty, slow, unfair).
---

# The playtest loop

Every time the person plays (`project_play`, or Play in the editor), the
play is recorded, and F8 marks moments they want you to see.

1. `play_show` first, before reading code: it shows them the play as a
   timeline and gives you its scenes, the values that changed and their
   markers.
2. Look at the moment: `play_frame` and `play_state` with `at: "m1"` (a
   marker) or a frame; `play_inputs` for what they pressed and how long.
3. Say what you see and what you'll change; then change it and `build`.
4. `play_verify` on their play: it says whether it now goes differently,
   and which values end differently.
5. Offer `play_resume` at the marker: the game opens right at that moment
   for them to try the fix.

No recorded play yet? Ask them to play (`project_play`) and press F8 where
it goes wrong, or drive it yourself: `drive_start` with `record: true`,
then `play_show` on that play.
