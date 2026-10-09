# Breakout: driven headless playthrough

Made by `./playtest.sh playthrough.replay.txt` (JM_DRIVE=1, JM_RENDERER=none). The script reads the ball and paddle from `state` and holds the arrow keys to track the ball, then lets go so the ball is lost. Answers are cut at 400 characters; the 375 tracking rounds (state/keys/step 4) are not echoed, only a session check every 300 frames.

What it shows: Enter on the title loads the game (frame 31: scenes/game.scene.json, lives 3, score 0); ArrowRight 20 frames moves the paddle 0 -> 200, ArrowLeft 40 frames 200 -> -200; while tracking, score rises 10 -> 130 with lives still 3; hands off, lives go 3 -> 2 -> 1 -> 0 and the game-over screen shows the final score 00370; Enter returns to the title.

## Replay check

The driver recorded its inputs to `playthrough.replay.txt`. Replaying it (`JM_INPUT_REPLAY=playthrough.replay.txt JM_RENDERER=none JM_EXIT_AFTER_FRAMES=2676 JM_DUMP_DIR=/tmp/rp JM_DUMP_FRAMES=393,1593,2433,2673,2675 jm run`) gives state dumps byte-identical (cmp) to the same frames dumped during a second driven run, and that second run recorded a byte-identical replay file. Sessions in the replay dumps:

```
state_00393.json: "scene":"scenes/game.scene.json" "session":{"lives":3.0,"score":10.0}
state_01593.json: "scene":"scenes/game.scene.json" "session":{"lives":3.0,"score":130.0}
state_02433.json: "scene":"scenes/game.scene.json" "session":{"lives":1.0,"score":340.0}
state_02673.json: "scene":"scenes/gameover.scene.json" "session":{"lives":0.0,"score":370.0}
state_02675.json: "scene":"scenes/title.scene.json" "session":{"lives":0.0,"score":370.0}
```

## Transcript

```
{"errors":[],"frame":0,"ok":true,"ready":true,"scene":"scenes/title.scene.json"}
## Title screen
> step 30
{"errors":[],"frame":30,"ok":true}
> state scene ui
{"errors":[],"ok":true,"state":{"frame":29,"scene":"scenes/title.scene.json","ui":[{"entity":[0,0],"root":{"children":[{"children":[{"id":"title","rect":[448.0,270.0,384.0,60.0],"tag":"h1","text":"BREAKOUT"},{"class":["hint"],"rect":[432.0,370.0,416.0,20.0],"tag":"p","text":"Arrow keys move the paddle"},{"class":["hint"],"rect":[480.0,422.0,320.0,20.0],"tag":"p","text":"Press ENTER to start"}],"id
## Enter starts the game
> press Enter
{"ok":true}
> step 2
{"errors":[],"frame":32,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":31,"scene":"scenes/game.scene.json","session":{"lives":3.0,"score":0.0}}}
## Arrow keys move the paddle
> state tag=Paddle
{"errors":[],"ok":true,"state":{"entities":[{"components":{"BoxColliderComponent":{"blocksMask":0,"collidesWithMask":0,"halfHeight":10.0,"halfWidth":70.0,"layerMask":2,"offsetX":0.0,"offsetY":0.0},"ScriptComponent":{"failed":false,"script":"assets/scripts/paddle.ts"},"SpriteComponent":{"a":1.0,"b":1.0,"g":0.9,"r":0.9,"shadowAlpha":0.0,"shadowB":0.0,"shadowG":0.0,"shadowLayer":null,"shadowR":0.0,"s
paddle x = 0.0
> down ArrowRight
{"ok":true}
> step 20
{"errors":[],"frame":52,"ok":true}
> up ArrowRight
{"ok":true}
> step 1
{"errors":[],"frame":53,"ok":true}
> state tag=Paddle
{"errors":[],"ok":true,"state":{"entities":[{"components":{"BoxColliderComponent":{"blocksMask":0,"collidesWithMask":0,"halfHeight":10.0,"halfWidth":70.0,"layerMask":2,"offsetX":0.0,"offsetY":0.0},"ScriptComponent":{"failed":false,"script":"assets/scripts/paddle.ts"},"SpriteComponent":{"a":1.0,"b":1.0,"g":0.9,"r":0.9,"shadowAlpha":0.0,"shadowB":0.0,"shadowG":0.0,"shadowLayer":null,"shadowR":0.0,"s
paddle x after 20 frames of ArrowRight = 200.0
> down ArrowLeft
{"ok":true}
> step 40
{"errors":[],"frame":93,"ok":true}
> up ArrowLeft
{"ok":true}
> step 1
{"errors":[],"frame":94,"ok":true}
> state tag=Paddle
{"errors":[],"ok":true,"state":{"entities":[{"components":{"BoxColliderComponent":{"blocksMask":0,"collidesWithMask":0,"halfHeight":10.0,"halfWidth":70.0,"layerMask":2,"offsetX":0.0,"offsetY":0.0},"ScriptComponent":{"failed":false,"script":"assets/scripts/paddle.ts"},"SpriteComponent":{"a":1.0,"b":1.0,"g":0.9,"r":0.9,"shadowAlpha":0.0,"shadowB":0.0,"shadowG":0.0,"shadowLayer":null,"shadowR":0.0,"s
paddle x after 40 frames of ArrowLeft = -200.0
## Tracking the ball with the paddle for 1500 frames (keys chosen from state each 4 frames)
> state session
{"errors":[],"ok":true,"state":{"frame":393,"session":{"lives":3.0,"score":10.0}}}
> state session
{"errors":[],"ok":true,"state":{"frame":693,"session":{"lives":3.0,"score":50.0}}}
> state session
{"errors":[],"ok":true,"state":{"frame":993,"session":{"lives":3.0,"score":70.0}}}
> state session
{"errors":[],"ok":true,"state":{"frame":1293,"session":{"lives":3.0,"score":90.0}}}
> state session
{"errors":[],"ok":true,"state":{"frame":1593,"session":{"lives":3.0,"score":130.0}}}
> up ArrowRight
{"ok":true}
## Hands off: the ball falls past the paddle and lives go down
> step 120
{"errors":[],"frame":1714,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":1713,"scene":"scenes/game.scene.json","session":{"lives":3.0,"score":160.0}}}
> step 120
{"errors":[],"frame":1834,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":1833,"scene":"scenes/game.scene.json","session":{"lives":2.0,"score":160.0}}}
> step 120
{"errors":[],"frame":1954,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":1953,"scene":"scenes/game.scene.json","session":{"lives":2.0,"score":220.0}}}
> step 120
{"errors":[],"frame":2074,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":2073,"scene":"scenes/game.scene.json","session":{"lives":2.0,"score":290.0}}}
> step 120
{"errors":[],"frame":2194,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":2193,"scene":"scenes/game.scene.json","session":{"lives":2.0,"score":290.0}}}
> step 120
{"errors":[],"frame":2314,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":2313,"scene":"scenes/game.scene.json","session":{"lives":2.0,"score":340.0}}}
> step 120
{"errors":[],"frame":2434,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":2433,"scene":"scenes/game.scene.json","session":{"lives":1.0,"score":340.0}}}
> step 120
{"errors":[],"frame":2554,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":2553,"scene":"scenes/game.scene.json","session":{"lives":1.0,"score":370.0}}}
> step 120
{"errors":[],"frame":2674,"ok":true}
> state scene session
{"errors":[],"ok":true,"state":{"frame":2673,"scene":"scenes/gameover.scene.json","session":{"lives":0.0,"score":370.0}}}
## Game over screen
> state ui
{"errors":[],"ok":true,"state":{"frame":2673,"ui":[{"entity":[52,1],"root":{"children":[{"children":[{"rect":[424.0,265.0,432.0,60.0],"tag":"h1","text":"GAME OVER"},{"children":[{"id":"final","rect":[652.0,365.0,120.0,30.0],"tag":"span","text":"00370"}],"class":["score"],"rect":[508.0,365.0,264.0,30.0],"tag":"p","text":"SCORE 00370"},{"class":["hint"],"rect":[440.0,427.0,400.0,20.0],"tag":"p","tex
## Enter goes back to the title
> press Enter
{"ok":true}
> step 2
{"errors":[],"frame":2676,"ok":true}
> state scene
{"errors":[],"ok":true,"state":{"frame":2675,"scene":"scenes/title.scene.json"}}
## Final state (for the replay check)
> state scene session tag=Paddle
{"errors":[],"ok":true,"state":{"entities":[],"frame":2675,"scene":"scenes/title.scene.json","session":{"lives":0.0,"score":370.0}}}
> quit
{"ok":true}
```
