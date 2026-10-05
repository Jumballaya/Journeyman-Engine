# Content & data formats

All paths in project files are relative to the project root and use forward
slashes (`assets/sounds/boom.wav`), whether the game runs from `build/` or a
packed `.jm` archive.

## `.jm.json` — the game manifest

```json
{
  "name": "Strike Wing 1942",
  "version": "1.0.0",
  "engine": "../build/release/engine/journeyman_engine",
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
  }
}
```

- `assets` lists everything the game loads by path (scripts, prefabs, images,
  atlases, sounds, fonts, UI, CSS, shaders, bindings, maps, data). They are
  preloaded at startup and packed into archives. Entries can be globs: `*`
  matches within a folder, `**` across folders (`node_modules`, `build` and
  dot-folders never match). `jm build` writes the expanded list to
  `build/.jm.json`, so new files matching a pattern need no manifest edit.
  Keep atlas source images out of the patterns: the atlas packs them.
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
  "tags": ["player_bullet"]
}
```

## Components

| Component | JSON fields |
|---|---|
| `TransformComponent` | `position [x, y, z]` (z = draw order, higher on top), `scale [sx, sy]` (half size), `rotation` (radians) |
| `SpriteComponent` | `texture` (image path or `atlas.json#region`), `color [r,g,b,a]`, `texRect [u,v,w,h]`, optional `shadow {x,y,scale,layer,color}` ([details](runtime-gameplay.md#sprite-shadows)) |
| `SpriteAnimationComponent` | `atlasPath`, `animations { name: { regions: [...], frameDuration, loop } }`, `current` |
| `VelocityComponent` | `velocity [vx, vy]`, `acceleration [ax, ay]` (added to the velocity every second, e.g. gravity) |
| `BoxColliderComponent` | `halfExtents [hx, hy]` (alias `size`), `offset [x, y]`, `layerMask`, `collidesWithMask` |
| `LifetimeComponent` | `seconds` — destroys the entity when it runs out |
| `ScrollWrapComponent` | `minY`, `maxY` — wraps y into the range (endless backgrounds) |
| `ScriptComponent` | `script`, `params { ... }`, `runWhenPaused` |
| `UIDocumentComponent` | `src` (`.ui.html`), `order` (higher draws on top) |
| `AudioEmitterComponent` | `sound` (name or path), `gain`, `looping`, `bus` (`"sfx"` or `"music"`); plays when the entity appears, fades out when it is destroyed |
| `TextComponent` | `text`, `size` (px), `color` (`"#rrggbb"` or `[r,g,b,a]`), `font` (path; default the UI font), `align` (`left`/`center`/`right`), `shadow` (color, 1px offset), `crisp`; drawn at the entity over the sprites, under the UI |
| `TileMapComponent` | `tileset` (path or object), `rows` (array, or a text file path), `vars`, `tileSize`, `outside`; see *Tile maps* |

**Collisions.** Two colliders interact when either one's `layerMask`
intersects the other's `collidesWithMask`, and at least one of them moves: it
has a `VelocityComponent` or has changed position at least once (pairs that
never move are skipped). Both entities' scripts get `onCollide(other)` every
frame they overlap.

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

A map is ASCII rows, one character per tile, top row first, drawn by a
tileset (`*.tileset.json`) that says what each character is:

```json
{ "atlas": "assets/atlases/sprites.atlas.json",
  "tiles": {
    ".": { "image": "grass" },
    "#": { "image": "{theme}ground", "solid": true,
           "edges": [{ "open": "N", "image": "{theme}ground_top" }] },
    ",": { "image": "path_{mask}", "joins": ",<>" },
    "~": { "image": "water_{mask}_{frame}", "frames": 3, "frameDuration": 0.35, "solid": true },
    "*": { "image": ["lava_1", "lava_2"], "tags": ["deadly"] },
    "E": { "solid": true, "under": ",." },
    "c": { "image": "cloud", "anchor": "bottom-left" } } }
```

- `image` names an atlas region (or a full image reference). `{frame}` with
  `frames`, or a list of names, animates it every `frameDuration` seconds.
- Edge-aware tiles: `{mask}` picks one of 16 images by which sides border a
  different terrain (1 = north, 2 = east, 4 = south, 8 = west); `joins` lists
  the characters counted as the same terrain (default: itself). `edges` rules
  pick an image instead: the first rule whose `open` sides all border another
  terrain and whose `closed` sides don't.
- `under` draws another tile beneath: the first of its characters found next
  to the tile, else the last (a person standing on a road or on grass).
- `solid` blocks `TileBody` movement; `tags` are for `map.is(tx, ty, tag)`.
- Images are drawn at their own size, centered on the cell's bottom edge, or
  growing up and right from the cell with `"anchor": "bottom-left"`.
- `{name}` in image names comes from the map's `vars`, so one tileset can
  serve several looks.
- Characters with no definition are empty and open: use them to mark where
  scripts spawn things (`map.positionsOf("ek")`).

The entity with a `TileMapComponent` sits at the map's bottom-left corner; its
z is the layer's draw order. `outside` is the character beyond the edges (one
for all sides, or `{"left", "right", "top", "bottom"}`). Only tiles in view are
drawn, with no entity per tile.

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
`align-items` (stretch, start, center, end) · `gap` · `flex` / `flex-grow` ·
`position` (static, relative, absolute) with `top right bottom left inset` ·
`width height min-* max-*` (px, %, vw, vh, auto) ·
`margin` (incl. `auto` centering) · `padding` · `border border-* border-width border-color` ·
`background` / `background-color` / `background-image: url(...)` ·
`color` · `font-size` · `font-family` (a font asset path) · `line-height` ·
`letter-spacing` · `text-align` · `text-transform: uppercase` · `text-shadow` ·
`opacity` · `visibility` · `z-index` (among siblings) ·
`font-smooth: never` (1-bit glyphs for pixel fonts).
Colors: `#rgb #rgba #rrggbb #rrggbbaa rgb() rgba()` and common names.

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
`Gamepad.`. Keyboard keys are matched by physical position.

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
| Resolver | one UTF-8 JSON object keyed by source path: each entry's offset and size in the payload, its `type` and optional metadata |

The manifest is stored under the key `.jm.json`. The engine reads the whole
archive into memory when it opens it.
