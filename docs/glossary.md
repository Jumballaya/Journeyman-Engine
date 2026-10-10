# Glossary: Journeyman next to Godot, Unity, Box2D and Tiled

Each Journeyman term next to its nearest equivalent in Godot 4, Unity 6 (2D),
Box2D v3 and Tiled. **≈** means close but not the same; **—** means there is
none. The last column says what differs when it matters. Look terms up here
before guessing from another engine: some names match and behave differently.

## Space and units

| Journeyman | Godot | Unity | Difference |
|---|---|---|---|
| world units | pixels | units (100 px each by default) | 1 unit = 1 pixel at zoom 1 |
| y up | y down | y up | Godot code ported as is flips vertically |
| `rotation` (radians, counter-clockwise) | `rotation` (radians, clockwise on screen) | `eulerAngles.z` (degrees, counter-clockwise) | |
| `z` (draw order, higher in front) | `z_index` | `sortingOrder` ≈ | one float on the transform; a child's adds to its parent's |
| `scale` (TransformComponent) | `scale` | `localScale` | **half size in pixels**, not a multiplier: `[16, 16]` draws a 32 px quad, `[1, 1]` a 2 px one |
| `transform` (world) / `local` (from the parent) | `global_position` / `position` | `position` / `localPosition` | Godot's `position` is local; Journeyman's `transform.x` is world, like Unity's |

## Entities, scenes and prefabs

| Journeyman | Godot | Unity | Tiled | Difference |
|---|---|---|---|---|
| entity | Node | GameObject | object ≈ | an id plus components; no class per kind |
| component | node type, child node ≈ | Component | — | data only; behavior is a script (`ScriptComponent`) |
| scene (`*.scene.json`) | scene (`.tscn`) | Scene | map ≈ | a list of entities; loading one destroys the last one's entities |
| prefab (`*.prefab.json`) | PackedScene (`.tscn`) | Prefab | template (`.tx`) ≈ | |
| `overrides` (scene entry) | instance property overrides | prefab overrides | — | merged recursively; arrays are replaced |
| `children`, `parent`, `attach()` / `detach()` | `add_child`, `get_parent`, `reparent()` | `SetParent(p, true)` | — | `attach` keeps the world position, from the end of the frame |
| tag (an entry's `name` is one too) | group | tag | — | many per entity |
| `World.find(tag)` / `findAll` | `get_first_node_in_group` / `get_nodes_in_group` | `FindWithTag` / `FindGameObjectsWithTag` | — | |
| `spawn(prefab, x, y, overrides)` | `instantiate()` + `add_child` | `Instantiate` | — | the entity appears at the end of the frame; the handle works now |
| `destroy()` | `queue_free()` | `Destroy()` | — | deferred in all three: Journeyman and Godot at the end of the frame, Unity after the current Update loop (before rendering) |
| `isAlive` | `is_instance_valid()` | `obj != null` | — | false as soon as `destroy()` is called; Godot's and Unity's stay true until the object is actually destroyed |
| `Entity.NONE` | `null` | `null` | — | a value, never null |
| scene `group`, `Scene.spawnGroup` | — | additive scene load ≈ | — | entries wait in the scene until a script asks |
| scene entry `if` / `unless` | — | — | — | spawns only if a `GameState` key is (not) truthy |
| `Scene.load` / `Scene.transition` | `change_scene_to_file` | `SceneManager.LoadScene` | — | `transition` crossfades or runs a shader |
| `World` (the ECS registry) | SceneTree ≈ | — | — | not Tiled's `.world` (a set of maps) |

## Components

`jm schema` lists every key with its default; `jm docs content` describes each.

| Journeyman | Godot | Unity | Box2D | Difference |
|---|---|---|---|---|
| `TransformComponent` `position`, `scale`, `rotation` | Node2D | Transform | body position, angle | see *Space and units*: `scale` is half size |
| `SpriteComponent` `texture`, `color`, `texRect` | Sprite2D `texture`, `modulate`, `region_rect` | SpriteRenderer `sprite`, `color` | — | `texRect` is `[u, v, w, h]` in 0..1, not pixels; no texture draws a solid quad |
| `SpriteAnimationComponent` `animations`, `current` | AnimatedSprite2D + SpriteFrames, `animation` | Animator + AnimationClip ≈ | — | frames are atlas regions; `frameDuration` in seconds, not FPS |
| `VelocityComponent` `velocity` | CharacterBody2D `velocity` | Rigidbody2D `linearVelocity` | `b2Body_SetLinearVelocity` | pixels per second |
| `VelocityComponent` `acceleration` | gravity (project setting) ≈ | `gravityScale` ≈ | world gravity × `gravityScale` ≈ | per entity; no global gravity |
| `VelocityComponent` `motion: "free"` | position += velocity × delta | Kinematic Rigidbody2D ≈ | kinematic body ≈ | passes through everything |
| `VelocityComponent` `motion: "move"` | `motion_mode = FLOATING` + `move_and_slide` ≈ | `Rigidbody2D.Slide` ≈ | character mover ≈ | stops at solids; zeroes the blocked axis |
| `VelocityComponent` `motion: "walk"` | `motion_mode = GROUNDED` + `move_and_slide` | `Rigidbody2D.Slide` ≈ | — | slopes to 50° (Godot's `floor_max_angle` defaults to 45°), 1-unit steps, one-way platforms |
| `BoxColliderComponent` `halfExtents`, `offset` | CollisionShape2D + RectangleShape2D `size` | BoxCollider2D `size`, `offset` | `b2MakeBox(hw, hh)` | half sizes, as in Box2D (Godot and Unity take full sizes); always axis-aligned: ignores the transform's rotation and scale |
| `layerMask` | `collision_layer` | GameObject `layer` | `categoryBits` | the layers it is **on**; Unity's `LayerMask` is a query filter instead |
| `collidesWithMask` | `collision_mask` | Layer Collision Matrix ≈ | `maskBits` | a pair touches when **either** side wants the other; Box2D needs both |
| `blocksMask` (`collider.solid`) | StaticBody2D vs Area2D ≈ | `isTrigger = false` | `isSensor = false` | **not solid by default** (0); Unity and Godot bodies are solid unless made triggers/areas |
| `CircleColliderComponent` | CircleShape2D | CircleCollider2D | `b2Circle` | never solid; `move()` uses the box |
| `GroundComponent` `chains`, `oneWay` | CollisionPolygon2D (segments), `one_way_collision` | EdgeCollider2D, PlatformEffector2D | chain shape | lines, not areas; no `onCollide`. Not Tiled's or Godot's *terrain* (autotiling) |
| `LifetimeComponent` `seconds` | Timer + `queue_free` | `Destroy(obj, t)` | — | |
| `ParticleEmitterComponent` `rate`, `burst`, `emitting` | CPUParticles2D `amount`, `emitting`, `one_shot` ≈ | ParticleSystem emission rate, bursts | — | `burst(n)` emits n now, like Unity's `Emit(n)` |
| `ScrollWrapComponent` | Parallax2D `repeat_size` ≈ | — | — | wraps y between two heights |
| `TextComponent` | Label ≈ | TextMeshPro | — | drawn in the world, under the UI |
| `UIDocumentComponent` `src`, `order` | CanvasLayer + Control, `layer` ≈ | UIDocument (UI Toolkit), `sortingOrder` | — | HTML/CSS subset instead of UXML/USS |
| `AudioEmitterComponent` `gain`, `looping`, `bus` | AudioStreamPlayer `volume_db`, `bus` | AudioSource `volume`, `loop`, mixer group | — | plays on spawn, like `autoplay` / `playOnAwake`; not positional; `gain` is linear, not dB |
| `ScriptComponent` `script`, `params`, `runWhenPaused` | attached script, `@export` vars, `process_mode = ALWAYS` | MonoBehaviour, serialized fields | — | one WebAssembly instance per entity: costly to create |
| `TileMapComponent` `map` | TileMapLayer | Tilemap | — | draws a Tiled `.tmj` |
| `NetworkComponent` `authority`, `replicate`, `interpolate` | MultiplayerSynchronizer, multiplayer authority | NetworkObject + NetworkTransform (Netcode) | — | |

## Scripts

| Journeyman | Godot | Unity | Difference |
|---|---|---|---|
| top-level code | `_ready()` | `Awake` / `Start` | runs once, when the entity's components exist |
| `onUpdate(dt)` | `_process(delta)` | `Update()` + `Time.deltaTime` | headless runs use a fixed 1/60 s |
| `onCollide(other)` | `body_entered` / `area_entered` ≈ | `OnTriggerStay2D` | **every frame** the two overlap, not once on entry (swept: a fast body that crossed counts); a pair where neither ever moved is skipped |
| `onMessage(m)`, `entity.send(name)` | signal, `call()` ≈ | `SendMessage` ≈ | queued: arrives before the receiver's next update |
| `World.broadcast(tag, name)` | `call_group` | — | by tag; Unity's `BroadcastMessage` goes to children instead |
| `self()` | `self` | `gameObject` | |
| `Params.number(key)` | `@export` var | serialized field | read from the scene's `params` |
| `entity.data` (`Store`) | `set_meta` / `get_meta` | — | dies with the entity |
| `GameState` | autoload ≈ | static class ≈ | survives scene changes; the driver and dumps call it `session` |
| `Save` | `ConfigFile` in `user://` ≈ | `PlayerPrefs` | written at the end of a frame that changed it |
| `Input.down(action)` | `is_action_pressed` | `GetButton` / `IsPressed()` | held |
| `Input.pressed(action)` | `is_action_just_pressed` | `GetButtonDown` / `WasPressedThisFrame()` | this frame only. Godot's `pressed` means held |
| `Input.released(action)` | `is_action_just_released` | `GetButtonUp` / `WasReleasedThisFrame()` | |
| `Input.value`, `Input.axis(neg, pos)` | `get_action_strength`, `get_axis(neg, pos)` | `ReadValue` | |
| `Camera.setPosition`, `Camera.zoom` | Camera2D `position`, `zoom` | Camera, `orthographicSize` | `zoom = 2` is twice as close (as in Godot 4; Unity's size is the inverse) |
| `PostEffect.custom(shader)` | CanvasItem shader on a full-screen ColorRect ≈ | URP Renderer Feature ≈ | runs on the whole frame, per scene |
| `Time.scale`, `Time.pause()` | `Engine.time_scale`, `get_tree().paused` | `Time.timeScale` | |
| `Time.unscaledDelta` | — | `Time.unscaledDeltaTime` | |
| `Timer`, `Tween`, `Timeline` | Timer, Tween, AnimationPlayer ≈ | — (coroutines) | ticked by the script: `tick(dt)` (`Timeline`: `update(dt, handler)`) |
| `Path`, `Swing` | Path2D + PathFollow2D, PinJoint2D ≈ | — | math helpers; the script moves the body |
| `CameraFollow` | Camera2D smoothing, drag margins | Cinemachine ≈ | |
| `Net` | `MultiplayerAPI` | Netcode `NetworkManager` | |

## Moving bodies

| Journeyman | Godot (CharacterBody2D) | Unity | Box2D | Difference |
|---|---|---|---|---|
| `entity.move(dx, dy, slide)` | `move_and_slide()` (floating) ≈ | `Rigidbody2D.Slide` ≈ | — | a displacement, not a velocity; x then y, flush |
| `entity.walk(dx, dy, dropThrough)` | `move_and_slide()` (grounded) | `Rigidbody2D.Slide` ≈ | — | |
| `Blocked` (what `move`/`walk` return) | `KinematicCollision2D` ≈ | `Collision2D` ≈ | — | |
| `hitX`, `hitY`, `byX`, `byY` | `get_slide_collision()` ≈ | — | — | -1/+1 per axis, and the entity in the way |
| `onGround` (`Blocked`, `velocity`) | `is_on_floor()` | `isGrounded` (CharacterController) | — | |
| `velocity.onWall`, `velocity.blockedX` | `is_on_wall()`, `get_wall_normal()` | — | — | `blockedX` is -1/+1: the side |
| `velocity.onCeiling` | `is_on_ceiling()` | — | — | |
| `normalX`, `normalY` | `get_floor_normal()` | `ContactPoint2D.normal` | manifold normal | |
| `velocity.floor` | `get_last_slide_collision().get_collider()` ≈ | — | — | the entity it stands on, or `Entity.NONE` |
| `velocity.platformVelocityX/Y` | `get_platform_velocity()` | — | — | 0 for a lift moved by `move()` |
| moving platform | moving platform (AnimatableBody2D) | moving platform | — | a solid mover (or moving ground) moves what stands on it |
| one-way platform (`platform` class, `oneWay`) | `one_way_collision` | PlatformEffector2D | one-sided chain ≈ | |
| `dropThrough` | — | — | — | falls through one-way platforms while on |
| `Physics.raycast(x, y, dx, dy, distance, ignore, mask)` | `intersect_ray()` | `Physics2D.Raycast` | `b2World_CastRayClosest` | |
| `RayHit` `entity`, `x`/`y`, `normalX`/`normalY`, `distance` | result dict `collider`, `position`, `normal` | `RaycastHit2D` | `b2RayResult` | |
| `Physics.overlapCircle` / `overlapBox` / `at` | `intersect_shape()` / `intersect_point()` | `OverlapCircleAll` / `OverlapBoxAll` / `OverlapPointAll` | overlap queries ≈ | `overlapBox` takes half sizes |
| `JM_DEBUG_PHYSICS=1`, `debug physics on` | `--debug-collisions` | Physics Debugger ≈ | debug draw | |

## Tile maps and ground

Maps are Tiled's own files, so Tiled's names mostly hold.

| Journeyman | Tiled | Godot | Unity | Difference |
|---|---|---|---|---|
| tile `type` | Class (`type` before Tiled 1.9) | custom data layer ≈ | — | what `map.at(x, y)` returns |
| `solid` tile property | custom property | physics layer ≈ | TilemapCollider2D ≈ | blocks `TileBody` |
| tile (0, 0) | top-left | (0, 0) at the origin, y down | bottom-left ≈ | **bottom-left**: rows count up |
| `map.objects(type)`, `MapObject` | object layer objects | — | — | in world units, y up |
| `ground` / `platform` object class | class on a polyline, polygon or rectangle | — | — | becomes the map entity's `GroundComponent` |
| `terrains` in a tileset | Terrain Set (wang set) | TileSet terrain set | Rule Tile ≈ | autotiling, unrelated to `GroundComponent` |
| `outside` map property | custom property | — | — | the tile type beyond the edges |
| `TileBody` | — | — | — | a box that moves against solid tiles |

## Driver, plays and markers

No other engine has these; the closest ideas:

| Journeyman | Closest idea | What it is |
|---|---|---|
| driver (`JM_DRIVE=1`, `jm plays drive`, MCP `drive_*`) | an RL environment's `step()` | the game advances only on `step`; one JSON line per command |
| `step`, `until`, `state`, `get`, `near` | frame advance, wait-until, world dump | `state` is the state dump; `near` lists colliders and ground nearby |
| state dump (`JM_DUMP_DIR`) | save-state snapshot | entities, `session` (the `GameState` values), save, UI layout, draw list |
| play (`.jm/plays/<id>`) | input-recording replay | inputs, timing, seed and save: replays exactly |
| marker (F8, driver `marker`) | bookmark ≈ | a moment the person flagged, with a screenshot and the state |
| moment (`420`, `12.5s`, `m2`, `end`) | timestamp | a frame, time or marker in a play |
| `jm plays verify` | replay regression test | does the play still go the same? |
| `jm plays resume` | load a save-state | the person plays on from that moment |
| golden (`jm golden`) | golden-image / snapshot test | reference frames that must not change |

## CLI and environment

| Journeyman | Godot | Unity | Difference |
|---|---|---|---|
| `jm run` | `godot --path .` | Play mode | runs `build/` |
| `jm build` | import step ≈ | asset import + script compile ≈ | checks scenes and prefabs against `jm schema` |
| `jm export` | `godot --export-release` | `BuildPipeline.BuildPlayer` | |
| `jm pack` (`.jm` archive) | `.pck` | asset bundle ≈ | |
| `jm test` | GUT / gdUnit4 | Test Runner | logic only: no rendering or audio |
| `jm schema` | — | — | every component's keys, as JSON |
| `jm generate` | — | — | starting templates (scaffolding) |
| `jm fmt` | `gdformat` ≈ | — | the editor's JSON layout |
| `JM_HEADLESS=1` | — | `-batchmode` | a hidden window that **still renders**; Godot's `--headless` is `JM_RENDERER=none` |
| `JM_RENDERER=none` | `--headless` | `-batchmode -nographics` | no window or GL |
| `JM_FIXED_DT` | `--fixed-fps` | `Time.captureDeltaTime` | |
| `JM_EXIT_AFTER_FRAMES` | `--quit-after` | — | |
| `JM_CAPTURE_DIR` + `JM_CAPTURE_FRAMES` | `--write-movie` ≈ | Recorder ≈ | chosen frames as PNGs |
| `JM_ENTRY_SCENE` | `godot <scene.tscn>` | — | |
| `JM_SESSION` | — | — | sets `GameState` values before the first frame |
