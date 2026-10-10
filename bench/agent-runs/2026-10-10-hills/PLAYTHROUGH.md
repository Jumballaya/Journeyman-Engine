# Hills: headless playthrough

Driven with the engine's stepped driver, no window and no GPU:
`JM_DRIVE=1 JM_RENDERER=none JM_STRICT=1 jm run`, commands on stdin, one JSON
answer per command. The run's inputs were recorded (`JM_DRIVE_RECORD`) to
`.jm/playthrough.replay.txt`; replaying it reaches the same frames (used for
the PNGs below). Driver script: `.jm/play.sh` (bash coprocess, because some
steps wait on a condition: "until the lift is at the bottom").

Level (world units, y up): solid ground line from a left wall along y=-200,
hill up to y=-140 between x=-350 and 30, flat stretch x 30..300, a 16-unit
step up at x=300, edge at x=620 down to a lower floor at y=-320; one-way
platform y=-130, x 100..220; lift at x=1000 moving y -280..0; coin at
(1000, 36). Player box is 16x24, so standing y = ground + 12.

Each sample line is `x y blockedY camX` (blockedY -1 = standing on something;
camX is the camera's x, which the player script copies into the session
because the driver's state has no camera).

```
## Start: player stands on the left flat
  x=-550.0 y=-188.0 blockedY=-1.0 camX=-60.0
## Walk right up and over the hill (y follows the slope, blockedY stays -1)
  down ArrowRight -> {"ok":true}
  x=-520.0 y=-188.0 blockedY=-1.0 camX=-60.0
  x=-490.0 y=-188.0 blockedY=-1.0 camX=-60.0
  x=-460.0 y=-188.0 blockedY=-1.0 camX=-60.0
  x=-430.0 y=-188.0 blockedY=-1.0 camX=-60.0
  x=-400.0 y=-188.0 blockedY=-1.0 camX=-60.0
  x=-370.0 y=-188.0 blockedY=-1.0 camX=-60.0
  x=-340.0 y=-180.79 blockedY=-1.0 camX=-60.0
  x=-310.0 y=-168.79 blockedY=-1.0 camX=-60.0
  x=-280.0 y=-156.79 blockedY=-1.0 camX=-60.0
  x=-250.0 y=-144.79 blockedY=-1.0 camX=-60.0
  x=-220.0 y=-132.79 blockedY=-1.0 camX=-60.0
  x=-190.01 y=-127.994 blockedY=-1.0 camX=-60.0
  x=-160.01 y=-127.994 blockedY=-1.0 camX=-60.0
  x=-130.01 y=-127.994 blockedY=-1.0 camX=-60.0
  x=-100.01 y=-132.786 blockedY=-1.0 camX=-60.0
  x=-70.00999 y=-144.786 blockedY=-1.0 camX=-60.0
  x=-40.00999 y=-156.786 blockedY=-1.0 camX=-60.0
  x=-10.00999 y=-168.786 blockedY=-1.0 camX=-54.043304443359375
  x=19.99001 y=-180.786 blockedY=-1.0 camX=-24.043310165405273
  x=49.99001 y=-187.99 blockedY=-1.0 camX=5.9566850662231445
  x=79.99001 y=-187.99 blockedY=-1.0 camX=35.95668411254883
  x=109.99 y=-187.99 blockedY=-1.0 camX=65.9566879272461
  up ArrowRight -> {"ok":true}
  x=109.99 y=-187.99 blockedY=-1.0 camX=71.58220672607422
## Under the one-way platform (x 100..220, top y -130): jump straight up through it
  press Space -> {"ok":true}
  x=109.99 y=-123.49 blockedY=0.0 camX=86.27354431152344
  x=109.99 y=-117.99 blockedY=-1.0 camX=89.9720687866211
  state tag=Player VelocityComponent -> {"errors":[],"ok":true,"state":{"entities":[{"components":{"VelocityComponent":{"ax":0.0,"ay":-900.0,"blockedX":0.0,"blockedY":-1.0,"dropThrough":0,"motion":2,"supportGeneration":0,"supportIndex":0,"supportVX":0.0,"supportVY":0.0,"vx":0.0,"vy":0.0}},"id":[12,0],"tags":["Player"]}],"frame":283}}
## Walk right off the platform, jump up the step at x 300
  down ArrowRight -> {"ok":true}
  x=259.99 y=-134.49 blockedY=0.0 camX=215.9872589111328
  x=291.99 y=-187.99 blockedY=-1.0 camX=258.48175048828125
  state tag=Player VelocityComponent -> {"errors":[],"ok":true,"state":{"entities":[{"components":{"VelocityComponent":{"ax":0.0,"ay":-900.0,"blockedX":1.0,"blockedY":-1.0,"dropThrough":0,"motion":2,"supportGeneration":0,"supportIndex":0,"supportVX":0.0,"supportVY":0.0,"vx":0.0,"vy":0.0}},"id":[12,0],"tags":["Player"]}],"frame":348}}
  press Space -> {"ok":true}
  x=402.99 y=-112.99 blockedY=0.0 camX=359.0645751953125
## Keep running right and fall off the edge at x 620 to the lower floor (y -320)
  x=612.99 y=-171.99 blockedY=-1.0 camX=568.956787109375
  x=762.99 y=-307.99 blockedY=-1.0 camX=660.0
  up ArrowRight -> {"ok":true}
## Walk to the lift's shaft (x 1000) and wait until the lift is near the bottom
  down ArrowRight -> {"ok":true}
  up ArrowRight -> {"ok":true}
  x=927.99 y=-307.99 blockedY=-1.0 camX=660.0
  lift.y=-90.66692
  lift.y=-278.6667
  get tag=Lift VelocityComponent.vy -> 80.0
## Jump and steer right onto the lift
  press Space -> {"ok":true}
  down ArrowRight -> {"ok":true}
  up ArrowRight -> {"ok":true}
  x=981.99 y=-204.6566 blockedY=-1.0 camX=660.0
  state tag=Player VelocityComponent -> {"errors":[],"ok":true,"state":{"entities":[{"components":{"VelocityComponent":{"ax":0.0,"ay":-900.0,"blockedX":0.0,"blockedY":-1.0,"dropThrough":0,"motion":2,"supportGeneration":0,"supportIndex":13,"supportVX":0.0,"supportVY":79.99969,"vx":0.0,"vy":0.0}},"id":[12,0],"tags":["Player"]}],"frame":886}}
  lift.y=-222.6666
## Step to the middle of the lift (under the coin)
  down ArrowRight -> {"ok":true}
  up ArrowRight -> {"ok":true}
  x=999.99 y=-195.3233 blockedY=-1.0 camX=660.0
## Ride the lift up; coin is at y 36 above it
  score=0.0
  lift.y=-200.0
  score=0.0
  lift.y=-186.6668
  score=0.0
  lift.y=-173.3335
  score=0.0
  lift.y=-160.0002
  score=0.0
  lift.y=-146.6669
  score=0.0
  lift.y=-133.3336
  score=0.0
  lift.y=-120.0003
  score=0.0
  lift.y=-106.6669
  score=0.0
  lift.y=-93.33359
  score=0.0
  lift.y=-80.00023
  score=0.0
  lift.y=-66.66687
  score=0.0
  lift.y=-53.33354
  score=0.0
  lift.y=-40.00022
  score=0.0
  lift.y=-26.66689
  score=0.0
  lift.y=-13.33356
  score=0.0
  lift.y=-0.0002224445
  score=10.0
  state tag=sparkle ParticleEmitterComponent -> {"errors":[],"ok":true,"state":{"entities":[{"components":{"ParticleEmitterComponent":{"angle":90.0,"burst":0,"emitting":1,"rate":0.0}},"id":[16,0],"tags":["sparkle"]}],"frame":1053}}
  get tag=Coin TransformComponent.x -> {"error":"no entity tagged Coin has TransformComponent.x","ok":false
  state tag=sparkle ParticleEmitterComponent -> {"errors":[],"ok":true,"state":{"entities":[{"components":{"ParticleEmitterComponent":{"angle":90.0,"burst":0,"emitting":1,"rate":0.0}},"id":[16,0],"tags":["sparkle"]}],"frame":1053}}
  state tag=Coin -> {"errors":[],"ok":true,"state":{"entities":[],"frame":1053}}
  state ui -> {"errors":[],"ok":true,"state":{"frame":1053,"ui":[{"entity":[15,0],"root":{"children":[{"children":[{"id":"score","rect":[16.0,12.0,160.0,25.0],"tag":"span","text":"SCORE 10"}],"id":"hud","rect":[16.0,12.0,160.0,25.0],"tag":"div","text":"SCORE 10"}],"rect":[0.0,0.0,1280.0,720.0],"tag":"root"}}]}}
  quit -> {"ok":true}
```

Player y while riding (sampled every 10 frames, last 17): -195.3233 -181.9901 -168.6568 -155.3235 -141.9902 -128.6569 -115.3236 -101.9903 -88.65695 -75.32359 -61.99023 -48.65688 -35.32355 -21.99023 -8.656898 4.676439 18.00978 
-- it tracks the lift's top (lift.y + 18) to the top of the path (player 18.01).

Particles at collection: `state draw` at frame 1053 lists 53 world sprites:
13 level sprites (11 ground lines, player, lift) + 40 particles of the
sparkle burst (burst: 40). The coin entity is gone and the HUD reads
`SCORE 10`.

## Extra checks

- Grounded every frame over the hill: stepping 220 single frames from x -550
  to ~110 with ArrowRight held, `get tag=Player VelocityComponent.blockedY`
  answered -1.0 on all 220 frames (no bounce up or down the slopes).
- Jump only when standing: pressing Space while falling off the platform
  (frame 334, blockedY 0) did nothing; it was the first thing that went
  wrong in my script, and the transcript above jumps after landing.
- Camera: camX stays clamped at the left bound (-60) until the player passes
  x≈-10, then follows (dead zone 40, smoothing 8) and clamps at the right
  bound (660).

## Frames (software GL under xvfb-run, physics debug on)

`.jm/frames/start.png` (frame 1) and `.jm/frames/burst.png` (frame 1057,
4 frames after the coin): ground and one-way platform lines, the player box,
the lift with the player on it and the burst, HUD "SCORE 10".

## Proof commands

- `jm build` -> Build complete!, `jm build --json` -> {"errors":0,"result":"ok","warnings":0}
- `jm test` -> 3 pass, 0 fail (tests/rules.spec.ts)
- `jm fmt` then `jm fmt --check` -> clean
