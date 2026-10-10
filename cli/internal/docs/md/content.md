# Content & data formats

All paths in project files are relative to the project root and use forward
slashes (`assets/sounds/boom.wav`), whether the game runs from `build/` or a
packed `.jm` archive.

## `.jm.json` — the game manifest

```json
{
  "name": "Strike Wing 1942",
  "version": "1.0.0",
  "entryScene": "scenes/title.scene.json",
  "scenes": ["scenes/title.scene.json", "scenes/level1.scene.json"],
  "assets": ["assets/scripts/*.ts", "assets/prefabs/*.prefab.json", "assets/sounds/*.wav", "..."],
  "scriptLibraries": { "@demos/common": "../common" },
  "config": {
    "window":   { "width": 540, "height": 720, "resizable": true, "vsync": true,
                  "fullscreen": false, "hideCursor": true },
    "renderer": { "logicalWidth": 480, "logicalHeight": 640,
                  "clearColor": [0.16, 0.43, 0.69, 1], "letterboxColor": [0, 0, 0, 1] },
    "ui":       { "defaultFont": "assets/fonts/PressStart2P-Regular.ttf" },
    "export":   { "icon": "assets/icon.png", "bundleId": "com.example.mygame" }
  },
  "net": { "topology": "server", "port": 7777, "maxPlayers": 8, "playerPrefab": "player",
           "server": { "entryScene": "scenes/arena.scene.json", "scripts": ["assets/scripts/server/rules.ts"] } }
}
```

- `assets` lists everything the game loads by path (scripts, prefabs, images,
  atlases, sounds, fonts, UI, CSS, shaders, bindings, maps, data). They are
  preloaded at startup and packed into archives. Entries can be globs: `*`
  matches within a folder, `**` across folders (`node_modules`, `build` and
  dot-folders never match). `jm build` writes the expanded list to
  `build/.jm.json`, so new files matching a pattern need no manifest edit.
  Keep atlas source images out of the patterns: the atlas packs them.
  A `.ts` asset ships as a script when content names it (a
  `ScriptComponent` in a scene, prefab, map or data file, `.jm.json` itself
  (a server's `net.server.scripts`), or a quoted
  `"assets/scripts/x.ts"` in a script, as spawn overrides attach one); other `.ts` files
  are modules scripts import, compiled into them. Every one is compiled, so
  errors show either way.
- `net` makes the game multiplayer, with what its dedicated server does
  differently under `net.server`; see [networking.md](networking.md). The
  `NetworkComponent` there shares an entity with every player.
- `scriptLibraries` maps an import name to a folder of shared scripts,
  relative to the project; see [scripting.md](scripting.md#building-scripts).
- `renderer.logicalWidth/Height` fix the game's coordinate space: world units
  equal logical pixels at zoom 1, the origin is the screen center with y up,
  and the image is scaled to the window with letterboxing. Omit them to use
  raw framebuffer pixels.

## Scenes

```json
{
  "name": "level1",
  "entities": [
    { "name": "Player", "prefab": "assets/prefabs/player.prefab.json",
      "overrides": { "TransformComponent": { "position": [0, -200, 7] } } },
    { "name": "HUD", "components": {
        "UIDocumentComponent": { "src": "assets/ui/hud.ui.html", "order": 0 },
        "ScriptComponent": { "script": "assets/scripts/hud.ts" } } }
  ]
}
```

An entity either lists `components` or instantiates a `prefab` (optionally
with `overrides`, merged recursively; arrays are replaced). `name` becomes a
tag. Loading a scene destroys the previous scene's entities, including
everything spawned at runtime.

### Children

Any entry (in a scene or a prefab) can hold others in `children`. A child
moves and turns with its parent and is destroyed with it; its
`TransformComponent` is relative to the parent: `position` is an offset
(turned with the parent), `z` adds to the parent's (draw order), `rotation`
adds to the parent's. Scale is the child's own (it's a sprite's size).

```json
{ "name": "Hero", "prefab": "assets/prefabs/hero.prefab.json",
  "children": [
    { "name": "Shadow", "prefab": "assets/prefabs/shadow.prefab.json",
      "overrides": { "TransformComponent": { "position": [0, -12, -4] } } }
  ] }
```

A prefab's own `children` come with every instance. An instance changes them
by name under `overrides.children`:

```json
{ "prefab": "assets/prefabs/knight.prefab.json",
  "overrides": { "children": { "Sword": { "SpriteComponent": { "texture": "...#axe" } } } } }
```

Prefabs may hold instances of other prefabs (never themselves). `group`, `if`
and `unless` apply to top-level entries: children come with their parent.

### Groups and conditions

Content is authored in the scene, not spawned by scripts, and three optional
keys decide when an entry appears:

```json
{ "name": "Bat", "prefab": "assets/prefabs/bat.prefab.json", "group": "room-1-0" },
{ "name": "Key", "prefab": "assets/prefabs/key.prefab.json", "unless": "done.crypt.7.10" },
{ "name": "Shard", "prefab": "assets/prefabs/shard.prefab.json", "if": "done.boss" }
```

- `group`: the entry waits until a script calls `Scene.spawnGroup("room-1-0")`;
  `Scene.despawnGroup` removes the members still alive. The dungeon demo
  swaps a room's group in as the hero walks into it.
- `if` / `unless`: a `GameState` key that must be (or must not be) truthy
  (true, nonzero or non-empty) when the entry would spawn. Taken items and
  opened doors stay gone this way.

## Prefabs

```json
{
  "components": {
    "TransformComponent": { "position": [0, 0, 6], "scale": [16, 16] },
    "SpriteComponent": { "texture": "assets/atlases/game.atlas.json#bullet" },
    "VelocityComponent": { "velocity": [0, 760] },
    "BoxColliderComponent": { "halfExtents": [4, 12], "layerMask": 2, "collidesWithMask": 4 },
    "LifetimeComponent": { "seconds": 1.0 }
  },
  "tags": ["player_bullet"],
  "children": [
    { "name": "Glow", "components": { "TransformComponent": { "position": [0, -10, -1], "scale": [6, 6] },
                                       "SpriteComponent": { "texture": "assets/atlases/game.atlas.json#glow" } } }
  ]
}
```

`children` is optional (see *Children* above).

## File layout

The editor and `jm` write JSON the same way: two-space indent, keys in the
order they came, short arrays of numbers or strings on one line
(`"position": [0, 20, 1]`), whole numbers as integers, a newline at the end.
Write files however you like; `jm fmt` puts them in that layout (and
`jm fmt --check` says which aren't), so a file reads the same whoever wrote
it and moving a sprite is a one-line diff.

## Components

The engine describes every component itself: `jm schema` prints each one's
keys (kind, default, range, choices, accepted asset types) and the fields
scripts can reach, as JSON (`jm schema SpriteComponent` for one;
`journeyman_engine --schema` underneath). `jm build` checks scenes and
prefabs against it and warns about unknown components, unknown keys and
values of the wrong kind, naming the file, entity and key.

| Component | JSON fields |
|---|---|
| `TransformComponent` | `position [x, y, z]` (z = draw order, higher on top), `scale [sx, sy]` (half size), `rotation` (radians) |
| `SpriteComponent` | `texture` (image path or `atlas.json#region`; without one, a solid quad in `color`), `color [r,g,b,a]`, `texRect [u,v,w,h]`, optional `shadow {x,y,scale,layer,color}` ([details](runtime-gameplay.md#sprite-shadows)) |
| `SpriteAnimationComponent` | `atlasPath`, `animations { name: { regions: [...], frameDuration, loop } }`, `current` |
| `VelocityComponent` | `velocity [vx, vy]`, `acceleration [ax, ay]` (added to the velocity every second, e.g. gravity), `motion` (`"free"`: through everything; `"move"` / `"walk"`: through solids and drawn ground like `entity.move()` / `walk()`; needs a box collider, or terrain for moving ground) |
| `BoxColliderComponent` | `halfExtents [hx, hy]`, `offset [x, y]`, `layerMask`, `collidesWithMask`, `blocksMask` |
| `TerrainComponent` | `chains [{points: [[x, y], ...], closed, oneWay}]`, `layerMask`: ground as lines (see *Drawn ground*) |
| `CircleColliderComponent` | `radius`, `offset [x, y]`, `layerMask`, `collidesWithMask`: a round collider. Never solid, and `move()` goes by an entity's box, not its circle |
| `LifetimeComponent` | `seconds` — destroys the entity when it runs out |
| `ScrollWrapComponent` | `minY`, `maxY` — wraps y into the range (endless backgrounds) |
| `ScriptComponent` | `script`, `params { ... }`, `runWhenPaused` |
| `UIDocumentComponent` | `src` (`.ui.html`), `order` (higher draws on top) |
| `AudioEmitterComponent` | `sound` (name or path), `gain`, `looping`, `bus` (`"sfx"` or `"music"`); plays when the entity appears, fades out when it is destroyed |
| `TextComponent` | `text`, `size` (px), `color` (`"#rrggbb"` or `[r,g,b,a]`), `font` (path; default the UI font), `align` (`left`/`center`/`right`), `shadow` (color, 1px offset), `crisp`; drawn at the entity over the sprites, under the UI |
| `TileMapComponent` | `map` (a Tiled `.tmj` file); see *Tile maps* |

**Collisions.** Box and circle colliders collide with each other by their
shapes (an entity with both touches another once a frame). Two colliders
interact when either one's `layerMask`
intersects the other's `collidesWithMask`, and at least one of them moves: it
has a `VelocityComponent` or has changed position at least once (pairs that
never move are skipped). Both entities' scripts get `onCollide(other)` every
frame they overlap. A body is checked along the whole way it went this frame:
its velocity's path, or the way `move()`/`walk()` (or velocity motion) took it,
over hills and as far as it got, and where a platform carried it. So a fast
bullet can't pass through a thin enemy between two frames; setting its
position in a script (a teleport) isn't swept.

**Solid colliders.** A collider with a `blocksMask` is solid to the layers in
it: an entity on one of them moving with `entity.move(dx, dy)` stops flush
against it instead of passing through (walls, crates, other characters).
Velocity doesn't stop at solids; a script moves the entity with `move` instead.
`blocksMask` defaults to 0, solid to nothing.

**Short names.** Wherever a script names a prefab, scene, shader or sound,
the file name without its extensions works (`"bullet"`, `"level2"`, `"crt"`,
`"laser"`) if the file is listed in the manifest.

## Atlases & sprite animation

An atlas config lists loose PNGs; `jm build` packs them into one texture and
records each image's region under its file name (without extension):

```json
{ "sources": ["assets/textures/ship.png", "assets/textures/bullet.png"],
  "filter": "nearest", "padding": 2 }
```

Reference regions as `assets/atlases/game.atlas.json#ship`. Use `padding` ≥ 1
when sprites move at sub-pixel positions to avoid neighbors bleeding in.

## Tile maps

Maps and tilesets are [Tiled](https://www.mapeditor.org) files, Tiled's JSON
formats: `*.tmj` maps and `*.tsj` tilesets. Paint them in the editor or in
Tiled; both read and write the same files, so you can switch freely between
the two.

```json
{ "TileMapComponent": { "map": "assets/maps/town.tmj" } }
```

The entity sits at the map's bottom-left corner, and its z is where the map
draws. Tile (0, 0) is the bottom-left; only tiles in view are drawn, with no
entity per tile.

**Tilesets.** A tileset is a collection of images (one per tile) or one sheet
image cut into a grid. Tile images are drawn at their own size, growing up and
right from their cell (Tiled's way, plus the tileset's tile offset). `jm build`
packs a collection's images into one sheet. What a tile means comes from
Tiled's per-tile fields:

- **type** (Tiled's *Class*): what scripts call it: `map.at(x, y) == "water"`.
- **properties**: `solid` (bool) blocks `TileBody` movement; any other bool is
  a tag for `map.is(x, y, "deadly")`; the rest scripts can read. Where layers
  stack, the topmost tile that sets a property decides it, so a bridge with
  `solid: false` crosses solid water, and leaving `solid` unset on decoration
  keeps the wall under it solid.
- **animation**: frames of the tileset's tiles, each with its duration.
- **terrains** (wang sets): which edges or corners of each tile show which
  terrain. The editor's terrain brush and Tiled's both pick the right tile
  for each cell from them: paths that join up, coasts, walls.

**Maps.** All of Tiled's orthogonal, finite map features draw:

- tile layers, in order (a layer's custom float property `z` adds to the
  entity's z instead, to draw treetops or roofs over the sprites), with
  visibility, opacity, tint, offsets and parallax; group layers pass theirs on;
- flipped and rotated tiles; animated tiles;
- image layers (backgrounds), repeating if set;
- object layers: rectangles, points, polylines and polygons for scripts
  (`map.objects("exit")`, with their names, types, properties and points),
  and tile objects, drawn.

**Drawn ground.** Organic levels draw their ground rather than tiling it: on
an object layer, a polyline, polygon or rectangle whose class (type) is
`ground` is solid terrain, and `platform` is one-way (held from above, jumped
up through; a platform rectangle is its top edge). Draw the lines along the
painted art's surfaces. Ground is lines, not areas (a closed shape is its
outline); object rotation applies, layer parallax doesn't, hidden layers
count, and an ellipse, point or tile object can't be ground (reported). The
map's ground becomes its entity's `TerrainComponent` on layer 1 from the first
frame (and again after `map.load`): rays and overlaps (`Physics`) hit it and
answer with the map's entity, and `walk()` takes bodies on its layers over it
(up slopes to 50°, steeper is a wall; onto platforms from above; `move()`
treats it as walls and floors). A scene can
also hold terrain itself: `TerrainComponent` with
`chains: [{"points": [[x, y], ...], "closed": false, "oneWay": false}]`,
relative to its entity.

Map properties: `outside` names the tile type beyond every edge (a solid one
keeps bodies in), or `outsideLeft`, `outsideRight`, `outsideTop`,
`outsideBottom` per side. Tile layer data may be CSV or uncompressed base64;
infinite maps and compressed layers aren't supported (set the map's *Tile
Layer Format* to CSV in Tiled).

## Data files

Any JSON or text file in the manifest can be read by scripts with
`Data.json(name)` / `Data.text(path)` (see
[scripting.md](scripting.md#data-files)), so tables of enemies, items or
dialog can live outside the code.

## UI (HTML/CSS)

A `.ui.html` file is a screen made of a forgiving HTML subset plus CSS from
`<style>` blocks and `<link rel="stylesheet" href="...">` (list linked `.css`
files in the manifest too). Lengths are logical pixels.

```html
<link rel="stylesheet" href="assets/ui/theme.css">
<style>
  #hud { position: absolute; top: 0; left: 0; right: 0; display: flex; justify-content: space-between; }
  .value { font-size: 16px; color: #fff; text-shadow: 2px 2px #000; }
</style>
<div id="hud">
  <div>SCORE <span id="score" class="value">0</span></div>
  <img id="life1" src="assets/atlases/game.atlas.json#ship" style="width: 24px; height: 24px">
</div>
```

**Elements.** Any tag works as a box; `span b i strong em a small label` are
inline (text runs), `img` draws `src` (image path or atlas region), `br`
breaks lines, `h1–h3`/`p` have default sizes and margins. Entities
(`&amp; &nbsp; &copy; &#169;` …) are decoded.

**Selectors.** `tag`, `.class`, `#id`, `*`, compounds (`div.item.selected`),
descendant (`a b`) and child (`a > b`) combinators, comma lists. Specificity
and source order follow CSS; `!important` is honored. Pseudo-classes are
ignored.

**Properties.**
`display` (block, flex, inline, none) ·
`flex-direction` · `justify-content` (start, center, end, space-between/around/evenly) ·
`align-items` (stretch, start, center, end) · `gap` · `flex` / `flex-grow` · `flex-wrap` ·
`position` (static, relative, absolute) with `top right bottom left inset` ·
`width height min-* max-*` (px, %, vw, vh, em, rem, auto) ·
`margin` (incl. `auto` centering) · `padding` · `border border-* border-width border-color` ·
`background` / `background-color` / `background-image: url(...)` ·
`color` · `font-size` · `font-family` (a font asset path) · `line-height` ·
`letter-spacing` · `text-align` · `text-transform: uppercase` · `text-shadow` ·
`opacity` · `visibility` · `z-index` (among siblings) ·
`font-smooth: never` (1-bit glyphs for pixel fonts).
Colors: `#rgb #rgba #rrggbb #rrggbbaa rgb() rgba()` and common names.

`flex-wrap: wrap` breaks a flex row into lines where the next item would
overflow (each line grows and justifies on its own; lines stack with `gap`
between them; columns don't wrap). `em` is the element's font size (a
`font-size` in `em` is its parent's), `rem` is 16px. Not supported:
pseudo-classes, floats and grid. Text directly inside a flex container
becomes its own flex item, as in CSS.

Absolutely positioned boxes without `top/bottom` (or `left/right`) are
centered on that axis in their container. Documents are re-laid out only
when a script changes them or the window resizes.

## Shaders

Post-effects and scene transitions are fragment shaders (`.frag`, listed in
the manifest). Unless the file has its own `#version`, the engine prepends:

```glsl
in vec2 v_texCoord;           // 0..1 across the render target
out vec4 outColor;
uniform sampler2D u_primary;  // the frame so far (transitions: incoming scene)
uniform sampler2D u_aux;      // transitions: last frame of the outgoing scene
uniform float u_progress;     // transitions: 0 = old scene … 1 = new scene
uniform vec2  u_resolution;   // render target size, pixels
uniform vec4  u_viewport;     // letterboxed game area, pixels (x, y, w, h)
uniform vec2  u_logical;      // logical resolution, e.g. 480x640
uniform float u_time;         // seconds
```

Declare your own `uniform`s and set them with `PostEffect.setFloat` /
`setVec2` / `setVec3` / `setVec4`.

## Input bindings

```json
{ "actions": {
    "fire":  ["Space", "Z", "Gamepad.A", "Gamepad.RightTrigger"],
    "left":  ["ArrowLeft", "A", "Gamepad.DPadLeft", "Gamepad.LeftStickLeft"],
    "pause": ["Escape", "P", "Gamepad.Start"] } }
```

Keys use the `Key` enum names (`A`–`Z`, `Digit0`–`Digit9`, `Space`, `Enter`,
`Escape`, `Tab`, `Backspace`, `ArrowUp/Down/Left/Right`, `F1`–`F24`,
`KP0`–`KP9`, `KPEnter`, punctuation like `Comma`, `Slash`, …). Gamepad
controls: `A B X Y LeftBumper RightBumper Back Start Guide LeftThumb
RightThumb DPadUp/Right/Down/Left LeftStickLeft/Right/Up/Down
RightStickLeft/Right/Up/Down LeftTrigger RightTrigger`, prefixed with
`Gamepad.`. Keyboard keys are matched by physical position. Mouse buttons
bind like keys: `MouseLeft`, `MouseRight`, `MouseMiddle`.

Every `*.bindings.json` among the manifest's assets loads at startup, with no
wiring; when two files name the same action, the one loaded later (manifest
order) replaces it. Scripts change bindings at run time with `Input.bind`.

## Audio & fonts

Sounds: `.wav`, `.ogg`, `.mp3`, `.flac` (decoded to 48 kHz stereo at load).
Fonts: `.ttf`/`.otf` (not `.ttc`); glyphs are rasterized on demand at the
on-screen size.
Without `config.ui.defaultFont` (or a loaded font), UI text uses a pixel font
built into the engine (Press Start 2P, SIL OFL — `engine/ui/fonts/`).

## Archive format (`.jm`)

`jm pack` / `jm export` write the whole game into one file (all integers
little-endian):

| Section | Contents |
|---|---|
| Header (32 bytes) | `u32` magic `"JMA1"`, `u32` version (1), `u64` payload offset, `u64` payload size, `u64` resolver offset (= payload offset + size) |
| Payload | the asset files' bytes, concatenated |
| Resolver | one UTF-8 JSON object keyed by source path: each entry's offset and size in the payload, its `type`, optional metadata, and `crc32` (IEEE) of its bytes |

The manifest is stored under the key `.jm.json`. The engine reads the whole
archive into memory when it opens it, and refuses it, naming the entry, if
any entry's bytes don't match its `crc32` (archives packed before checksums
have none, and still open).
