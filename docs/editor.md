# The Journeyman Editor

`journeyman_editor` is a desktop editor for Journeyman projects. It opens a
project folder (the one with `.jm.json`), shows its scenes the way the
engine draws them, plays the game inside a panel, and exports a standalone
executable. Every kind of project file has an editor: scenes and prefabs,
tilesets, atlases, input actions, data tables, UI screens and shaders.
Everything it changes is an ordinary project file, edited in place, so the
CLI, your text editor and git keep working alongside it.

```bash
./scripts/build-release.sh           # engine + editor
cd cli && go build -o ../build/bin/jm ./cmd/jm
./build/release/editor/journeyman_editor
```

The editor finds `jm` beside itself, in `../bin/`, in the repo's
`build/bin/`, or on `PATH` (or set `JM_CLI`). The first build
downloads the script compiler if the machine has none (see `jm doctor`).
`./scripts/package-editor.sh` makes a self-contained `Journeyman Editor.app`
(macOS) or folder (Linux) with `jm` and the engine inside.
On macOS, run `./scripts/install_mac.sh` to build the bundle and install it in
`/Applications`, replacing an older installation. Run it without `sudo`; it
requests administrator permission only if needed for installation.

## The workspace

| Area | What it's for |
|---|---|
| Toolbar | The scene switcher, the tools, Play / Pause / Step, build status, Export, and the command search |
| Hierarchy (left) | The scene's entities as a tree: search, select, rename, drag to reorder or nest, right-click for more. The eye hides an entity in the Scene view only |
| Scene (center) | The scene as the engine renders it, with a free camera, gizmos and tile painting |
| Game (center, tab) | The running game |
| Asset tabs (center) | Tilesets, atlases, input actions, data, UI screens and shaders open as tabs here (see [Asset editors](#asset-editors)) |
| Inspector (right) | The selected entities' components, or what's selected in the asset tab in use |
| Assets (bottom) | Project files as thumbnails. Drag them into the Scene, the Hierarchy or an Inspector field. Click one to preview it in the Inspector; drop files from your file manager to import them |
| Console (bottom, tab) | Build output and the game's log, cleared when play starts (toggle in its toolbar). Double-click a line naming a file to open it |
| Status bar | The project, play state, selection, cursor position, zoom, error/warning counts |

Panels dock anywhere. **View → Reset Layout** restores the default.
**View → Appearance** picks Dark, Light, or Match System (the default:
follows macOS or Windows, live; dark elsewhere).
The layout, recent projects, the last scene of each project and where the
Scene view was looking are remembered per user.

## Scene view

- **Pan:** middle-drag, right-drag, Space+drag, the Hand tool (H), or two-finger scrolling on a trackpad.
- **Zoom:** the mouse wheel, or Ctrl/Cmd+scroll on a trackpad, zooms toward the cursor.
- **F** frames the selection and **Home** frames the whole scene; the zoom menu (top right) has fixed levels.
- **Select:** click to select (Shift adds, Ctrl/Cmd toggles). Drag on empty space to box-select. Alt-click cycles through stacked entities.
- **Move (W):** drag an entity's body or the gizmo's center to move freely; drag an arrow for one axis. Moves land on whole pixels.
- **Rotate (E)** drags the ring; **Scale (R)** drags the corner handles (Shift scales uniformly).
- **Snapping:** the magnet (Shift+G) snaps to the grid (choose its size with the ruler); holding Ctrl flips snapping for one drag. Rotation snaps to 15°.
- **Nudge:** arrow keys move the selection by a pixel (Shift: a grid step).
- **Game UI:** UI screens (`UIDocumentComponent`) are laid out at the game's resolution and drawn inside the game frame, as the game will show them. Toggle with the screen icon or Ctrl+Shift+U.
- **Overlays:** the grid, colliders and terrain (off by default; drawn as the game draws them with `JM_DEBUG_PHYSICS`: solid boxes magenta, others green, circles cyan, ground white, one-way platforms yellow, a tile map's drawn ground included), and the game frame. The game frame is what the game's camera sees at the start, centered on the origin, with the world outside it dimmed slightly.
- **Shapes:** a selected entity's terrain (`TerrainComponent`) shows its lines and points: drag a point to move it, double-click a line to add one, double-click a point to remove it (a line keeps two points, a closed shape three). A circle collider shows its outline and a radius handle. A tile map's ground is drawn in Tiled, so it has none; turn on colliders and terrain to see it.
- **Create:** right-click empty space to create an entity there.
- **Drop:** drop an image or atlas region (sprite), prefab (instance), map (tile map) or tileset (a new map using it), `.ui.html` (UI screen), sound or script onto empty space to create an entity. Drop one onto an entity (in the view or on its Hierarchy row) to attach it: a script, picture, sound, UI document or map sets that entity's component; a tileset joins its map.

Entities with no visual (a script, a sound) show as small circles at their
position; entities without a transform (UI screens, game directors) appear
only in the Hierarchy.

## Inspector

Each component's fields come from the engine's component schemas
(`ComponentSpec::schema`), so components a module adds appear with proper
controls.

- **Numbers** drag to change. Click to type.
- **Vectors** have colored axis labels.
- **Colors** edit as hex or with the picker.
- **Layer masks** open a grid of 32 layers.
- **Asset fields** show a thumbnail, open a searchable picker, and accept files dragged from the Assets panel.
- **Right-click a field label** to reset it to its default, or revert a prefab override.
- **Prefab instances** show which prefab they come from. Fields you set are overrides, marked with an accent bar. A component's **...** menu can revert all of its overrides. Components added to an instance become overrides too.
- **Scripts** list the params they read. The editor finds `Params.number("speed", 150)` and `Params.text("theme")` in the script, with their types and defaults, so you set them without opening the code.
- **Tile maps** show their map's layers (show or hide, pick the one painted, add, rename with a double-click, reorder, delete, opacity and depth), its tilesets (open, remove, add or make one), the selected map object, and the map's size, outside tile and custom properties. Resizing keeps the bottom-left corner fixed. **Open in Tiled** hands the map to Tiled.
- **Add Component** searches every component the engine knows, grouped by category.

Editing with several entities selected changes all of them.

## Tile maps

Maps are Tiled files (`.tmj`, see [content.md](content.md#tile-maps)): paint
them here or in Tiled, which opens them as they are (**Open in Tiled** in the
Inspector, the palette, or a map's Assets entry). Edits made in Tiled show
here once saved there; unsaved painting here is saved first.

**Create → Tile Map** makes an entity and asks for the new map's name; it
paints with the project's first tileset. Dropping a tileset on the scene makes
a map that uses it; dropping one on a map entity adds it to that map.

Select a tile map entity and the tile tools appear in the toolbar:
- **Brush (B):** right-drag erases.
- **Rectangle (U):** fills the dragged area.
- **Fill (G):** floods the region of the clicked tile.
- **Eraser (X).**
- **Picker (I):** takes the topmost tile under the cursor, and its layer.

The palette in the Scene view's corner picks the layer painted on, the tile
(with tabs when the map has several tilesets), flips and quarter turns for the
next tiles, and the tileset's **terrains**: with one chosen, the brush,
rectangle and fill paint that terrain and choose each cell's tile so edges and
corners meet (paths join, coasts curve). A ghost of the tile follows the
cursor; hovering a cell shows its coordinates, type and layer.

With an object layer chosen, drag to draw a rectangle object (whole tiles;
Alt for pixels), drag one to move it, Delete to remove it; the Inspector
edits its name, type, position, size and properties.

Each stroke is one undo step. Painted maps are written to their files when
the scene saves.

## Assets

**New** (the + in the Assets panel) makes any kind of file from a working
template: scene, prefab, UI screen, script, post effect, transition,
stylesheet, tileset, tile map, atlas, data table or input actions. It asks for a name,
shows the file it will make, and opens it in its editor (a new scene is
saved and added to the game at once). New, imported and prefab files are added
to `.jm.json` when no entry there covers them, so builds take them; the
Inspector flags any file the game can't load, with a button that lists it.

Clicking a file shows it in the Inspector:
- images and atlas regions at whole-pixel scale;
- an atlas's regions, which drag into the scene;
- a sound's waveform, with Play;
- a font sample;
- a script's description, what runs when, the params it reads and where it's used;
- a map's **Paint in the Scene** (opens the scene that draws it, map selected, brush ready) and **Open in Tiled**;
- for scenes and prefabs, Open and Add to Scene;
- for files with an editor, Edit (double-click does the same);
- for UI screens, a picture of the screen as the game first shows it (also on their Assets tiles);
- for every file, where it's used, by path or by the short name scripts load it with.

With nothing selected, the Inspector shows the open scene: Play, what it
holds, its spawn groups, whether the game can load it and which scripts do.

Renaming or moving a file (rename it, or drag it onto a folder) rewrites
every reference to it:
- in scenes, prefabs and UI;
- in Tiled maps and tilesets, whose paths are relative to themselves;
- in scripts' path strings;
- in the manifest.

The scene Inspector's **Start the Game Here** makes a scene the one the game
opens with.

Deleting a file moves it to the editor's trash, with Undo in the notice;
deleting a scene also removes it from the manifest. Scripts that spawn
prefabs by short name (`spawn("coin")`) are not rewritten.

## Asset editors

Double-click a file (or **Edit** in the Inspector) to open its editor as a
tab beside Scene and Game. Edits there undo with **Ctrl/Cmd+Z** while the tab
is in use, and save themselves a moment after you stop (no Save, no
prompts); a change made by another program reloads, as an undoable step.
**Edit → Duplicate** and **Delete** act on what's selected in the tab, and
its properties show in the Inspector.

- **Input actions** (`*.bindings.json`): each action's keys and gamepad
  controls as chips. **+** then press the key or button to bind it (or
  click the waiting chip with a mouse button to bind that button); the
  gamepad menu lists every control. Shows which scripts read each action,
  and adds the ones scripts read but nothing defines. While the game runs,
  actions light up as it reads them.
- **Tilesets** (`*.tsj`, Tiled's): every tile as a card. **Add Tiles** picks
  images, one tile each; or cut one sheet image into tiles (the grid icon)
  at the tile size beside it. The Inspector edits a tile's type, whether it's
  solid (or passable over solid ground, for bridges), its properties (bools
  are tags), its image and its animation frames, and shows how often maps
  place it; **Paint with This Tile** opens a scene drawing a map that uses
  the tileset, with the tile in the brush. **Terrains** (+ to add one): pick
  a terrain and a color, then click the edges or corners of the cards that
  show it (right-click clears); the Inspector names the terrain, its colors,
  and whether tiles join by edges, corners or both. **Open in Tiled** is in
  the bar.
- **Atlases** (`*.atlas.json`): the packed images. Drag images or whole
  folders in from Assets, or **Add Images**; remove on hover. Name clashes
  and missing files are flagged; **Packed** shows the built texture.
- **Data** (other `.json`): lists of records edit as a spreadsheet, with
  the selected record in full in the Inspector; anything else as a typed
  tree. A file can hold several tables (**+ Table**; rename or delete one
  from its right-click menu). Typing in a cell replaces it, **Tab** walks
  the cells and out of the last one adds a row; columns are added with a
  type, and can be moved, retyped, renamed and deleted from their header's
  menu; the id column stays in view while scrolling. Cells know what they
  hold:
  - text naming atlas regions shows the picture, with the atlas picker;
  - text naming sounds plays them, with a sound picker;
  - text matching another table's ids (`"requires": "revolver"`) picks from
    that table by name and icon, and flags an id nothing has;
  - text columns that repeat a few values offer them in a dropdown.
  Empty columns go by their name (`icon`, `portrait`, `sound`, `quest`…).
- **UI screens** (`*.ui.html`): the screen drawn by the engine at the
  game's resolution. Click elements (or pick them in the outline), insert
  boxes, rows, columns, text and images, and edit text, id, classes and
  style in the Inspector, where faint values show what the element gets
  from its stylesheets. Drag an absolutely placed element to move it (it
  keeps its anchoring), drag the corner handle to resize, and drag rows in
  the outline to reorder or nest them. Double-click text to retype it.
  Selecting a part a script hides (class `hidden`) reveals it while it's
  selected. Changes are small edits to the HTML, so the file keeps its
  formatting; **Code** edits the HTML directly. Double-clicking a screen's
  element in the Scene view opens it here.
- **Shaders** (`.frag`): the code beside the open scene drawn through it,
  recompiled as you type, with errors by line. The Inspector has a control
  for each uniform; transitions loop or scrub their progress.

## Nesting

Entities hold others: a child moves and turns with its parent, and goes when
it's deleted. In the Hierarchy, children sit indented under their parent;
the caret (or Left and Right) folds and unfolds them.

- **Drag a row onto another** to put it inside; drop between rows to place
  it there, or below the list to take it out to the top level. It keeps
  where it stands in the world.
- **Create Child** (a row's right-click menu) adds an empty entity inside it;
  **Move Out of ...** lifts a child up a level.
- A child's Transform is relative to its parent (the Inspector says so), and
  the gizmo moves it in its parent's frame. Selecting a parent and its child
  moves them together.
- Duplicate, copy and delete take an entity's children with it.

## Prefabs

- **Making one:** drag an entity from the Hierarchy onto the Assets panel
  (or a folder in it), or use **Make Prefab** in its right-click menu. Its
  components and its children go to a `.prefab.json`, and the entity becomes
  an instance. **Create > Prefab** in Assets starts an empty one.
- **Parts:** an instance shows the children its prefab brings, dimmed under
  its caret; double-click one to edit it in the prefab.
- **Instances** show a bar at the top of the Inspector: the prefab's picture
  and name, **Overrides N**, and **Edit**. The overrides menu applies or
  reverts each component's changes, or all of them, with Undo.
- **Editing the prefab** (Edit, or double-click it in Assets) opens it in the
  Scene view under a banner, as a tree: its root and every part. What you
  create or drop there becomes a part of the root, including other prefabs
  (not the prefab itself). The scene stays open behind it, unsaved edits and
  all, and **Back** returns to it with every instance updated.
- **In Assets** a prefab's thumbnail is its picture. Selecting one shows its
  components, **Add to Scene**, and **Select N in This Scene**.
- **Unpack Prefab** turns an instance into plain components, its parts into
  the scene's own children.

## Groups and conditions

The Inspector's **Spawning** section decides when an entity appears in the
game (see [content.md](content.md#groups-and-conditions)):
- **Group** holds it back until a script spawns the group. Grouped entities
  sit under a header in the Hierarchy; drag rows between headers to regroup.
  The header's eye hides the whole group in the Scene view.
- **Only If** and **Unless** name game-state keys checked when it would spawn.

## Play

**Play Scene (Ctrl/Cmd+P or F6)** runs the open scene as it is in the editor,
saved or not, in the Game view. **Play Game (F5)** starts from the project's
first scene instead, with the open scene's unsaved edits in place when the
game reaches it. The caret beside Play offers both.
- **Building first:** if sources changed since the last build, the editor builds first.
- **Keyboard and mouse:** while the Game view has focus the game gets the keyboard; the mouse goes to the game while the pointer is over it. Click elsewhere to use editor shortcuts again; Play, Pause and Step still work.
- **Builds:** a build that fails (a script that doesn't compile) leaves the last good one in place, so the preview and Play keep working; its errors in the Console name the file and line, and double-clicking one opens it there.
- **Pause and Step:** Pause freezes the game, and **Step (F10)** advances one frame.
- **Scale:** **Fit** fills the view; **Pixel Perfect** uses whole-number scaling.
- **Running entities:** while playing, the Hierarchy's **Running** tab lists the
  live world, including what scripts spawned. Selecting one outlines it in the
  Game view, and the Inspector edits its transform and script fields live.
  These changes last until Stop.
- **Edits during play:** edits to the scene change the document, not the running game. Stop and play again to see them.
- **Saves:** play sessions use a separate save folder, so testing never touches a player's save.
- **Multiplayer:** **Play > Play with Players...** runs the whole session on
  this machine, in windows of their own: the game's dedicated server (if it
  has one) and a window per player, with optional simulated latency and loss
  (`jm run --peers`). Its output goes to the Console, and **Stop the
  Multiplayer Session** ends it. **Project Settings > Multiplayer** edits
  `.jm.json`'s `net`; see [networking.md](networking.md).

## Builds

The editor watches the project. When files change it runs `jm build` in the
background, then reloads the preview. Saving a scene or prefab doesn't need
a rebuild: the editor writes it into `build/` too. The toolbar shows
**Up to date**, **Changes pending** or the running build. Build errors
appear in the Console and as a notice.

## Export

**Export** builds one executable with the engine and every asset inside;
players need nothing installed.
- **macOS:** an `.app` (or a bare binary), signed ad hoc.
- **Windows:** an `.exe`.
- **Linux:** a single binary.

For a game with multiplayer settings, **What** picks the game or its
**Dedicated server**: one executable with `journeyman_server` and the game's
files inside (`jm export --server`), to run where players can reach it.

Exporting for another platform needs that platform's engine build (the
"player"). The `players` CI workflow builds them; put one at
`players/<os>-<arch>/journeyman_engine[.exe]` beside `jm`. The dialog shows
which platforms are ready.

Builds and exports run `jm` with your login shell's `PATH`, so your own Node
(if you have one) is found even when the editor is opened from Finder or the Dock. An export started
during a build waits for it, and one whose build is current skips rebuilding.

## Saving and recovery

Edits are undoable with descriptive names (**Edit → Undo Move Player**).
- **New scenes:** a new scene is named on its first save. **Save As** (Ctrl/Cmd+Shift+S) saves a copy under a new name.
- **Save:** **Ctrl/Cmd+S** writes the scene in the JSON layout `jm` writes too (`jm fmt`), so a file reads the same whoever wrote it and a small edit is a small diff.
- **Recovery:** unsaved work is copied to a recovery file every 20 seconds. If the editor stops without saving, opening the scene offers to restore it.
- **Switching or quitting:** with unsaved changes, the editor asks first.
- **Asset tabs** save on their own a moment after each change, and on close or quit.
- **Changes from other programs** (an agent editing the files, a text editor, git): the open scene and asset tabs reload them as one undoable step, **Change on Disk**. If you had unsaved edits, they're one Undo away, and a notice says so.
- **What's open here** is in `.jm/editor-session-<pid>.json` while the editor runs (the open files, and those with unsaved edits), so `jm build` and `jm doctor` can warn an agent before it changes a file you're editing.

## Pointing an agent at something

**Copy Reference** (right-click an entity, or Ctrl/Cmd+Shift+C) copies how to
name the selection outside the editor: its file and path of names,
`scenes/level1.scene.json#Hero/Sword` (`Bat[2]` for the second of siblings
sharing a name). A UI element with an id has its reference in the
Inspector (`assets/ui/hud.ui.html#score`), and an atlas region in the atlas
editor (`assets/atlases/ui.atlas.json#open`). Paste it to the agent.

**Copy Spot Reference** (right-click a place in the Scene view) copies a point in
the world, `scenes/level1.scene.json@120,-40` (y up), and, when it's on drawn
ground, whose: `(on the ground of scenes/level1.scene.json#Map)`. "This slope
at ... is too steep" then needs no guessing.

## Shortcuts

Ctrl means Cmd on macOS. **Help → Keyboard Shortcuts** lists every one.

| | |
|---|---|
| Command palette | Ctrl+K (or Ctrl+Shift+P). Type `>` for commands, `@` for entities |
| Save / Save As / New scene / Open scene | Ctrl+S / Ctrl+Shift+S / Ctrl+N / Ctrl+Shift+O |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z |
| Cut / Copy / Paste / Duplicate | Ctrl+X / Ctrl+C / Ctrl+V / Ctrl+D |
| Delete / Rename | Delete or Backspace / F2 |
| Select all / Deselect | Ctrl+A / Esc |
| Tools | Q select, W move, E rotate, R scale, H hand |
| Tile tools | B brush, U rectangle, G fill, X eraser, I picker |
| Frame selection / scene / 100% | F / Home / Ctrl+0 |
| Grid / Snap | Ctrl+' / Shift+G |
| Play scene / Play game / Pause / Step | Ctrl+P or F6 / F5 / Ctrl+Alt+P / F10 |
| Build / Export / Settings | Ctrl+B / Ctrl+Shift+E / Ctrl+, |
| Panels | Ctrl+1 Scene ... Ctrl+6 Console |

## Automation

The editor can run headless for screenshots and smoke tests:

```bash
JM_HEADLESS=1 JM_EDITOR_PROJECT=demos/dungeon JM_EDITOR_SCENE=scenes/grove.scene.json \
JM_EDITOR_SCRIPT="60:@select map;62:@key B;70:@mouse 700 300;72:@down;80:@mouse 900 300;82:@up" \
JM_EDITOR_CAPTURE=out.png JM_EDITOR_FRAMES=120 ./build/release/editor/journeyman_editor
```

`JM_EDITOR_SCRIPT` runs commands (`play.toggle`, `scene.save`...) at frame
numbers. It also simulates input:
- the mouse: `@mouse x y`, `@down`, `@up`, `@rdown`, `@rup`, `@mdown`, `@mup`,
  `@wheel dy`, `@scroll dx dy` (a trackpad); `@click x y`, `@dblclick`, `@rclick`,
  and `@drag`/`@rdrag`/`@mdrag x1 y1 x2 y2` expand to the steps a hand makes;
- keys: `@key W` (ImGui), `@press W` (a physical key, as the window delivers it: what
  the game and "press a key to bind" read), `@keydown W`/`@keyup W` to hold one,
  `@hold Space`/`@unhold Space`, and the modifiers `@ctrl`, `@shift`, `@super`, `@release`;
- selection and files: `@select Name`, `@inspect path`, `@open path` (an asset tab), `@move from to`,
  `@import file`, `@add asset`, `@apply asset`, `@makeprefab`;
- the running game: `@live tag` selects a running entity;
- text: `@type text`.

`JM_EDITOR_CAPTURE` saves the given frame as a PNG and quits;
`JM_EDITOR_SIZE` sets the window size; `JM_EDITOR_APPEARANCE=dark|light`
pins the appearance, so captures don't depend on the machine's setting.

`JM_EDITOR_CONTROL=dir` steers a running editor from outside instead: append
steps (one per line) to `dir/in` and they run one a frame; `@shot file.png`
saves the next frame, and `@done token` writes `token` to `dir/done` once
everything before it has run.

## How it works

The editor links the engine and runs it in place.
- **Hosting:** an `Engine` created with `EngineOptions::embedded` has no window. It renders into a texture the editor draws, takes input only when the editor forwards it, and runs one `frame()` at a time.
- **Scene preview:** a hosted engine with simulation off (`setSimulating(false)`) runs only the render stage. The editor respawns each scene entry as it changes and points the renderer at its own camera (`Renderer2DModule::setEditorView`).
- **Play:** a second hosted engine runs the game normally.

The scene document is the source of truth; the engines only display it.
