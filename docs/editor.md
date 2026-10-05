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
`build/bin/`, or on `PATH` (or set `JM_CLI`). Building scripts needs
Node.js; a new project installs AssemblyScript on its first build.
`./scripts/package-editor.sh` makes a self-contained `Journeyman Editor.app`
(macOS) or folder (Linux) with `jm` and the engine inside.

## The workspace

| Area | What it's for |
|---|---|
| Toolbar | The scene switcher, the tools, Play / Pause / Step, build status, Export, and the command search |
| Hierarchy (left) | The scene's entities: search, select, rename, drag to reorder, right-click for more. The eye hides an entity in the Scene view only |
| Scene (center) | The scene as the engine renders it, with a free camera, gizmos and tile painting |
| Game (center, tab) | The running game |
| Asset tabs (center) | Tilesets, atlases, input actions, data, UI screens and shaders open as tabs here (see [Asset editors](#asset-editors)) |
| Inspector (right) | The selected entities' components, or what's selected in the asset tab in use |
| Assets (bottom) | Project files as thumbnails. Drag them into the Scene, the Hierarchy or an Inspector field. Click one to preview it in the Inspector; drop files from your file manager to import them |
| Console (bottom, tab) | Build output and the game's log, cleared when play starts (toggle in its toolbar). Double-click a line naming a file to open it |
| Status bar | The project, play state, selection, cursor position, zoom, error/warning counts |

Panels dock anywhere. **View → Reset Layout** restores the default.
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
- **Overlays:** the grid, collider outlines (green), and the game frame. The game frame is what the game's camera sees at the start, centered on the origin, with the world outside it dimmed slightly.
- **Create:** right-click empty space to create an entity there.
- **Drop:** drop an image or atlas region (sprite), prefab (instance), tileset or map (tile map), `.ui.html` (UI screen), sound or script onto empty space to create an entity. Drop one onto an entity (in the view or on its Hierarchy row) to attach it: a script, picture, sound, UI document or tileset sets that entity's component.

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
- **Tile maps** show their size, a **Paint Tiles** button, and a size field. Resizing keeps the bottom-left corner fixed.
- **Add Component** searches every component the engine knows, grouped by category.

Editing with several entities selected changes all of them.

## Tile maps

Select a tile map entity and the tile tools appear in the toolbar:
- **Brush (B):** right-drag erases.
- **Rectangle (U):** fills the dragged area.
- **Fill (G):** floods the region of the clicked character.
- **Eraser (X).**
- **Picker (I):** takes a tile from the map.

The palette in the Scene view's corner shows the tileset's characters with
their images. Hovering a cell shows its coordinates and character.

Each stroke is one undo step. Maps stored in a `.txt` file are written back
to that file when you save; maps kept inline in the scene stay there.

## Assets

**New** (the + in the Assets panel) makes any kind of file from a working
template: scene, prefab, UI screen, script, post effect, transition,
stylesheet, tileset, atlas, data table or input actions. It asks for a name
and opens the file in its editor. New, imported and prefab files are added
to `.jm.json` when no entry there covers them, so builds take them; the
Inspector flags any file the game can't load, with a button that lists it.

Clicking a file shows it in the Inspector:
- images and atlas regions at whole-pixel scale;
- an atlas's regions, which drag into the scene;
- a sound's waveform, with Play;
- a font sample;
- a script's description, what runs when, the params it reads and where it's used;
- a map's rows, with **Paint in the Scene** (opens the scene that draws it, map selected, brush ready);
- for scenes and prefabs, Open and Add to Scene;
- for files with an editor, Edit (double-click does the same).

Renaming or moving a file (rename it, or drag it onto a folder) rewrites
every reference to it:
- in scenes, prefabs, tilesets and UI;
- in scripts' path strings;
- in the manifest.

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
  controls as chips. **+** then press the key or button to bind it; the
  gamepad menu lists every control. Shows which scripts read each action,
  and adds the ones scripts read but nothing defines.
- **Tilesets** (`*.tileset.json`): every tile as a card from the atlas. The
  Inspector edits a tile's image (pick from the atlas), animation frames,
  anchor, solidity, tags, what's drawn under it, joins and edge rules. An
  edge-aware tile previews as a patch of terrain, edges and all, and shows
  how often the maps place it. The look menu previews `{name}` vars.
- **Atlases** (`*.atlas.json`): the packed images. Drag images or whole
  folders in from Assets, or **Add Images**; remove on hover. Name clashes
  and missing files are flagged; **Packed** shows the built texture.
- **Data** (other `.json`): lists of records edit as a spreadsheet (add,
  duplicate and reorder rows; add, rename and delete columns), with the
  selected record in full in the Inspector; anything else as a typed tree.
- **UI screens** (`*.ui.html`): the screen drawn by the engine at the
  game's resolution. Click elements (or pick them in the outline), insert
  boxes, rows, columns, text and images, and edit text, id, classes and
  style in the Inspector. Double-click text to retype it. Selecting a part
  a script hides (class `hidden`) reveals it while it's selected. Changes
  are small edits to the HTML, so the file keeps its formatting; **Code**
  edits the HTML directly.
- **Shaders** (`.frag`): the code beside the open scene drawn through it,
  recompiled as you type, with errors by line. The Inspector has a control
  for each uniform; transitions loop or scrub their progress.

## Prefabs

- **Making one:** drag an entity from the Hierarchy onto the Assets panel
  (or a folder in it), or use **Make Prefab** in its right-click menu. Its
  components go to a `.prefab.json` and the entity becomes an instance.
  **Create > Prefab** in Assets starts an empty one.
- **Instances** show a bar at the top of the Inspector: the prefab's picture
  and name, **Overrides N**, and **Edit**. The overrides menu applies or
  reverts each component's changes, or all of them, with Undo.
- **Editing the prefab** (Edit, or double-click it in Assets) opens it in the
  Scene view under a banner. The scene stays open behind it, unsaved edits
  and all, and **Back** returns to it with every instance updated.
- **In Assets** a prefab's thumbnail is its picture. Selecting one shows its
  components, **Add to Scene**, and **Select N in This Scene**.
- **Unpack Prefab** turns an instance into plain components.

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
- **Keyboard:** while the Game view has focus the game gets the keyboard. Click elsewhere to use editor shortcuts again; Play, Pause and Step still work.
- **Pause and Step:** Pause freezes the game, and **Step (F10)** advances one frame.
- **Scale:** **Fit** fills the view; **Pixel Perfect** uses whole-number scaling.
- **Running entities:** while playing, the Hierarchy's **Running** tab lists the
  live world, including what scripts spawned. Selecting one outlines it in the
  Game view, and the Inspector edits its transform and script fields live.
  These changes last until Stop.
- **Edits during play:** edits to the scene change the document, not the running game. Stop and play again to see them.
- **Saves:** play sessions use a separate save folder, so testing never touches a player's save.

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

Exporting for another platform needs that platform's engine build (the
"player"). The `players` CI workflow builds them; put one at
`players/<os>-<arch>/journeyman_engine[.exe]` beside `jm`. The dialog shows
which platforms are ready.

Builds and exports run `jm` with your login shell's `PATH`, so Node is found
even when the editor is opened from Finder or the Dock. An export started
during a build waits for it, and one whose build is current skips rebuilding.

## Saving and recovery

Edits are undoable with descriptive names (**Edit → Undo Move Player**).
- **New scenes:** a new scene is named on its first save. **Save As** (Ctrl/Cmd+Shift+S) saves a copy under a new name.
- **Save:** **Ctrl/Cmd+S** writes the scene in the same JSON style as the file it came from, so diffs show only real changes.
- **Recovery:** unsaved work is copied to a recovery file every 20 seconds. If the editor stops without saving, opening the scene offers to restore it.
- **Switching or quitting:** with unsaved changes, the editor asks first.
- **Asset tabs** save on their own a moment after each change, and on close or quit.

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
- the mouse: `@mouse x y`, `@down`, `@up`, `@rdown`, `@rup`, `@wheel dy`;
- keys: `@key W`, `@ctrl`, `@shift`, `@release`;
- selection and files: `@select Name`, `@inspect path`, `@open path` (an asset tab), `@move from to`,
  `@import file`, `@add asset`, `@apply asset`, `@makeprefab`;
- the running game: `@live tag` selects a running entity;
- text: `@type text`.

`JM_EDITOR_CAPTURE` saves the given frame as a PNG and quits;
`JM_EDITOR_SIZE` sets the window size.

## How it works

The editor links the engine and runs it in place.
- **Hosting:** an `Engine` created with `EngineOptions::embedded` has no window. It renders into a texture the editor draws, takes input only when the editor forwards it, and runs one `frame()` at a time.
- **Scene preview:** a hosted engine with simulation off (`setSimulating(false)`) runs only the render stage. The editor respawns each scene entry as it changes and points the renderer at its own camera (`Renderer2DModule::setEditorView`).
- **Play:** a second hosted engine runs the game normally.

The scene document is the source of truth; the engines only display it.
