# Breakout: driven headless playthrough

Container `jmbox`, no display/GPU. Build + tests first:

```
$ jm build --json
{"errors":0,"result":"ok","warnings":0}
$ jm test
✔ rules: topRowScoresMost / rowRoundTrips / paddleStaysOnScreen / paddleEdgesAngleTheBall / sideVersusTopHits / threeMissesEndTheGame
ℹ tests 6  ℹ pass 6  ℹ fail 0
```

## Command

Driver commands are in `breakout/tests/playthrough.cmds`; the recorded inputs are in
`breakout/tests/playthrough.replay.txt` (`JM_DRIVE_RECORD`). `JM_STRICT=1` means
any script error would have ended the run with exit 1 (it exited 0).

```
JM_STRICT=1 JM_DRIVE=1 JM_RENDERER=none JM_DRIVE_RECORD=tests/playthrough.replay.txt \
  jm run < tests/playthrough.cmds | node /work/tools/summ.mjs
```

`summ.mjs` (in /work/tools) turns each `state` answer into a one-line summary:
scene, session (score/lives), number of bricks, Ball/Paddle position and
velocity, and the UI text. Raw `state` answers are several KB each.

Commands sent:
```
step 30
state
press Enter
step 5
state
down ArrowLeft
step 30
up ArrowLeft
state
down ArrowRight
step 45
up ArrowRight
state
step 100
state
step 100
state
step 100
state
step 100
state
step 100
state
step 100
state
step 1200
state
press Enter
step 5
state
quit
```

## Transcript (every answer, `state` summarized)

```
{"errors":[],"frame":0,"ok":true,"ready":true,"scene":"scenes/title.scene.json"}
{"errors":[],"frame":30,"ok":true}
{"frame":30,"scene":"scenes/title.scene.json","session":{},"bricks":0,"entities":{"Title":{}},"ui":{"title":"BREAKOUT","hint":"press Enter"}}
{"ok":true}
{"errors":[],"frame":35,"ok":true}
{"frame":35,"scene":"scenes/main.scene.json","session":{"lives":3,"score":0},"bricks":50,"entities":{"Ball":{"x":0,"y":-296,"vx":0,"vy":0},"Paddle":{"x":0,"y":-320},"Game":{}},"ui":{"div@16,8,112,20":"SCORE  0","div@1152,8,112,20":"LIVES  3"}}
{"ok":true}
{"errors":[],"frame":65,"ok":true}
{"ok":true}
{"frame":65,"scene":"scenes/main.scene.json","session":{"lives":3,"score":0},"bricks":50,"entities":{"Ball":{"x":-300,"y":-296,"vx":0,"vy":0},"Paddle":{"x":-300,"y":-320},"Game":{}},"ui":{"div@16,8,112,20":"SCORE  0","div@1152,8,112,20":"LIVES  3"}}
{"ok":true}
{"errors":[],"frame":110,"ok":true}
{"ok":true}
{"frame":110,"scene":"scenes/main.scene.json","session":{"lives":3,"score":0},"bricks":50,"entities":{"Ball":{"x":23.2,"y":-174.1,"vx":168,"vy":384.9},"Paddle":{"x":150,"y":-320},"Game":{}},"ui":{"div@16,8,112,20":"SCORE  0","div@1152,8,112,20":"LIVES  3"}}
{"errors":[],"frame":210,"ok":true}
{"frame":210,"scene":"scenes/main.scene.json","session":{"lives":3,"score":10},"bricks":49,"entities":{"Ball":{"x":303.2,"y":-238.3,"vx":168,"vy":-384.9},"Paddle":{"x":150,"y":-320},"Game":{}},"ui":{"div@16,8,128,20":"SCORE  10","div@1152,8,112,20":"LIVES  3"}}
{"errors":[],"frame":310,"ok":true}
{"frame":310,"scene":"scenes/main.scene.json","session":{"lives":2,"score":10},"bricks":49,"entities":{"Ball":{"x":194.8,"y":-193.4,"vx":168,"vy":384.9},"Paddle":{"x":150,"y":-320},"Game":{}},"ui":{"div@16,8,128,20":"SCORE  10","div@1152,8,112,20":"LIVES  2"}}
{"errors":[],"frame":410,"ok":true}
{"frame":410,"scene":"scenes/main.scene.json","session":{"lives":2,"score":20},"bricks":48,"entities":{"Ball":{"x":474.8,"y":-219,"vx":168,"vy":-384.9},"Paddle":{"x":150,"y":-320},"Game":{}},"ui":{"div@16,8,128,20":"SCORE  20","div@1152,8,112,20":"LIVES  2"}}
{"errors":[],"frame":510,"ok":true}
{"frame":510,"scene":"scenes/main.scene.json","session":{"lives":1,"score":20},"bricks":48,"entities":{"Ball":{"x":186.4,"y":-212.6,"vx":168,"vy":384.9},"Paddle":{"x":150,"y":-320},"Game":{}},"ui":{"div@16,8,128,20":"SCORE  20","div@1152,8,112,20":"LIVES  1"}}
{"errors":[],"frame":610,"ok":true}
{"frame":610,"scene":"scenes/main.scene.json","session":{"lives":1,"score":50},"bricks":46,"entities":{"Ball":{"x":203.2,"y":-122.8,"vx":-168,"vy":-384.9},"Paddle":{"x":150,"y":-320},"Game":{}},"ui":{"div@16,8,128,20":"SCORE  50","div@1152,8,112,20":"LIVES  1"}}
{"errors":[],"frame":710,"ok":true}
{"frame":710,"scene":"scenes/main.scene.json","session":{"lives":1,"score":60},"bricks":45,"entities":{"Ball":{"x":-52,"y":78,"vx":-147,"vy":-393.4},"Paddle":{"x":150,"y":-320},"Game":{}},"ui":{"div@16,8,128,20":"SCORE  60","div@1152,8,112,20":"LIVES  1"}}
{"errors":[],"frame":1910,"ok":true}
{"frame":1910,"scene":"scenes/gameover.scene.json","session":{"lives":0,"score":60},"bricks":0,"entities":{"GameOver":{}},"ui":{"title":"GAME OVER","p@576,370,128,20":"SCORE  60","hint":"press Enter"}}
{"ok":true}
{"errors":[],"frame":1915,"ok":true}
{"frame":1915,"scene":"scenes/title.scene.json","session":{"lives":0,"score":60},"bricks":0,"entities":{"Title":{}},"ui":{"title":"BREAKOUT","hint":"press Enter"}}
{"ok":true}
```

## What it shows

| Check | Evidence |
|---|---|
| Title screen | frame 30: scene `title`, UI `title`="BREAKOUT", `hint`="press Enter" |
| Enter starts the game | `press Enter`, frame 35: scene `main`, session `{lives:3, score:0}`, 50 bricks, HUD "SCORE 0" / "LIVES 3" |
| Paddle moves with arrows | 30 frames of ArrowLeft: Paddle x 0 -> -300; 45 frames of ArrowRight: -300 -> 150 (600 units/s) |
| Ball bounces | Ball vy flips sign (384.9 -> -384.9) on bricks/ceiling, and vx changes (168 -> -147) after hitting the paddle |
| Bricks break, score rises | frame 210: score 10, bricks 49; frame 610: score 50, bricks 46; frame 710: score 60, bricks 45 |
| Missing loses a life | frame 310: lives 2; frame 510: lives 1 (paddle left parked at x=150 on purpose) |
| 0 lives -> game over | frame 1910: scene `gameover`, UI "GAME OVER", "SCORE 60", "press Enter" |
| Enter -> title | frame 1915: scene `title` again |

## Replay of the recorded inputs

```
JM_RENDERER=none JM_INPUT_REPLAY=tests/playthrough.replay.txt JM_EXIT_AFTER_FRAMES=1915 \
  JM_DUMP_DIR=/work/dump JM_DUMP_FRAMES=710,1910 jm run
frame 710:  main, lives 1, score 60, bricks 45, Ball (-54.4, 71.5)   <- driven run had (-52, 78)
frame 1910: gameover, lives 0, score 60
```

Same outcome, but the replay runs one frame off the driven run (see FRICTION.md #2).
