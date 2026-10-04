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
  "assets": ["assets/scripts/player.ts", "assets/sounds/shoot.wav", "..."],
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
  atlases, sounds, fonts, UI, CSS, shaders, bindings). They are preloaded at
  startup and packed into archives. `jm generate` adds new files for you.
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
| `SpriteComponent` | `texture` (image path or `atlas.json#region`), `color [r,g,b,a]`, `texRect [u,v,w,h]` |
| `SpriteAnimationComponent` | `atlasPath`, `animations { name: { regions: [...], frameDuration, loop } }`, `current` |
| `VelocityComponent` | `velocity [vx, vy]` |
| `BoxColliderComponent` | `halfExtents [hx, hy]` (alias `size`), `offset [x, y]`, `layerMask`, `collidesWithMask` |
| `LifetimeComponent` | `seconds` — destroys the entity when it runs out |
| `ScrollWrapComponent` | `minY`, `maxY` — wraps y into the range (endless backgrounds) |
| `ScriptComponent` | `script`, `params { ... }`, `runWhenPaused` |
| `UIDocumentComponent` | `src` (`.ui.html`), `order` (higher draws on top) |
| `AudioEmitterComponent` | `sound` (name or path), `gain`, `looping`, `bus` (`"sfx"` or `"music"`); plays when the entity appears, fades out when it is destroyed |

**Collisions.** Two colliders interact when either one's `layerMask`
intersects the other's `collidesWithMask`, and at least one of them has a
`VelocityComponent` (static pairs are skipped). Both entities' scripts get
`onCollide(other)` every frame they overlap.

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
