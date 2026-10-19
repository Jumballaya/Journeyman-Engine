# {{name}}: working on a Journeyman game

This is a game for the Journeyman 2D engine. All of it is files: edit them,
then use `jm` to build, test and play. Nothing needs the editor, and nothing
asks for input. Every command named here exists, and `jm <command> --help`
covers each one. Before anything else: if jm's MCP tools (`play_show`,
`drive_start`, ...) aren't among your tools, connect them ("No MCP tools?"
below).

## Start here: the person plays, you look

You build this game with a person. They play it (`jm run`); every play is
recorded, and F8 marks moments they want you to see. When they mention
playing, how something feels (floaty, slow, unfair), a death, a bug, "my
marker" or "just now", or ask how the game is going: look at their latest
play **before** reading code or answering.

- **jm's MCP tools connected** (`play_show`, `build`, `drive_start`, ...)?
  Use them, not `jm` in a shell: call `play_show` first (it shows the person
  a timeline they can scrub, and gives you their markers), then `play_frame`
  / `play_state` at a marker (`at: "m1"`), `play_inputs` for what they
  pressed and how long. A marker with picks (a moment) says what they
  meant: `play_moment` shows the picks outlined, and each pick's `origin`
  is the file to change. After a change: `build`,
  `play_verify`, then offer `play_resume`.
- **No MCP tools?** Connect first: without them you can't see the game's
  frames from a sandbox. Run `jm setup` once (if your sandbox blocks it, ask
  the person to approve it), then tell them: "restart this app in this
  folder so I can see your plays and the game". Keep working meanwhile with
  `jm plays show`, `jm plays frame latest m1`, `jm plays state latest m1`;
  after a change, `jm build` then `jm plays verify`.

## Files

| Path | What it is |
|---|---|
| `.jm.json` | the manifest: name, entry scene, scene list, asset globs, window and renderer config |
| `scenes/*.scene.json` | scenes: entities, each with components (JSON) |
| `assets/prefabs/*.prefab.json` | entity templates that scripts `spawn("name", x, y)` |
| `assets/scripts/*.ts` | game logic in **AssemblyScript** (a TypeScript subset compiled to WebAssembly; it is not TypeScript) |
| `assets/ui/*.ui.html`, `*.css` | UI screens, in an HTML/CSS subset |
| `tests/*.spec.ts` | logic tests (`jm test`) |
| `tests/golden/` | reference frames (`jm golden`) |
| `design/*.md` | the game's memory: design notes (Pitch, Pillars, Levels, Systems, Decisions, Notes) and dossiers (`design/Characters/`, `Locations/`...) |
| `build/` | made by `jm build` and wiped each time. Never edit it |

New files under `assets/` ship when they match a pattern in `.jm.json`'s
`assets` (a new project's `assets/**` matches all of them; an older one may
list folders or extensions). After `jm build`, `build/.jm.json` lists what
ships: check a new file is there. Scenes
are listed in `.jm.json`'s `scenes`: `jm generate scene level2` makes one and
adds it, and `entryScene` is the one the game starts in.
`assets/input.bindings.json` maps actions (`left`, `confirm`, ...) to keys
for `Input.down("left")`; edit it to add your own. `jm generate list` shows the
starting templates; for example `jm generate script coin` and
`jm generate prefab bullet`. `jm init` already made `assets/scripts/player.ts`,
the Player's script (the arrow keys move it): edit it rather than generating one.

`design/` is what the game is and why, kept by you and the person: read it
(`jm memory`, `jm memory show Pillars`; over MCP `memory`) before you change how
the game plays. When they say how something feels or you find something in a
playtest, add it (`jm memory note "the pit in 1-1 is unfair" --in Levels
--category feel`; `memory_note`); when you settle something together, record
it (`jm memory decide "Jumps are variable height" "serves [[Pillars]]"`;
`memory_decide`). `[[Note]]` links notes; name game files in backticks. The
notes are an Obsidian vault and Basic Memory's format, so the person can open
them there too.

With a new game, plan it with the person first: `onboard` (`jm memory
onboard`) gives the steps (pitch, feel, pillars, scope, world, characters,
art, first level, and how the person likes to work), done or not as the notes
say, and the next question to ask. Ask it with a suggestion, keep the answer
where the step says, and call `onboard` again; `skip` one the game doesn't
need. The MCP prompt `new_game_interview` does the same.

The game's world goes in dossiers: a note per character, item, location,
faction, quest or piece of lore, in `design/<Folder>/` (`jm memory dossier
character "Mara Dorne" --set home="[[Irongate]]"`; `dossier_new`). Each kind
is a template in `design/_templates/` (its properties in frontmatter, its
sections in the body): `jm memory kinds` lists them, and a file there adds a
kind of the game's own. Link a dossier to what it relates to, in its
properties (`faction: "[[House Dorne]]"`) and its text, so what links to a
note (`jm memory show Irongate`) is how the world is connected. Read the
dossiers a new one relates to first, so it agrees with them.
`design/<Folder>.base` shows a kind as a table in Obsidian.

How the world connects is in those links: a property naming a note
(`region: "[[Ashvale]]"`), a typed relation in a note's Relations
(`- road_to [[Duskhold]] (distance: 120 km)`; `jm memory relate Irongate
road_to Duskhold --with distance="120 km"`, `world_link`), and plain
mentions. State a relation once: the other side reads it (`located_in` is
`contains` from the place, `road_to` and `at_war_with` read the same both
ways; a note named Relations adds `- [inverse] a b` or `- [symmetric] a`).
Before writing something new, look at what's around it: `jm memory world
Ashvale` (`world_context`) gives the notes within two steps, the connections
and the names linked that have no note yet; `jm memory path "Mara Dorne"
Ost` (`world_path`) how two connect. `health`'s world check finds what the
notes state twice and differently (a road of two lengths, a place inside
itself).

What holds in every conversation is a fact to keep in mind: the world's
canon, a rule, a constraint, what was tried and why it failed (`jm memory
remember "The Dornes were exiled from Varn" --category canon`; `remember`),
in `design/Memory.md`; the person's own preferences, for all their games,
with `--you` (scope you), in `~/.jm/memory.md`. One fact a call. Opening the
game (`open_game`, and a Codex or Claude session in its folder) reads them
back. When `remember` lists similar facts, `forget` one that's no longer so
or remember again with `--replaces <id>` (`replaces`); `jm memory recall
exile` (`recall`) finds them. A note is what came up; a fact is what to
keep in mind.

To grow what the game knows, follow a playbook (`playbook` lists them;
the same are MCP prompts): `expand_world` around a note (the second
capital, the families on both sides of a border), `new_dossiers` of a kind,
`level_from_place` (a location's dossier to a level brief and its scene),
`story_outline` (acts and quests from the factions and characters),
`canon_check` of a note against the facts and the world, and `art_pass`
(a dossier's sprite drawn in Aseprite: its `.aseprite` source in
`assets/art/`, the sheet in the atlas, and the dossier's sprite property
naming the source).

The game's history is git (`jm init` makes the first commit). After a change
that builds and plays right, `vcs_commit` it (`jm vcs commit "Make jumps
variable height"`). `vcs_status` (`jm vcs`) lists what changed, a scene's per
entity; `vcs_log`, `vcs_restore` (stashes what it would lose first). Prefer
them to raw git: they stay in the game's folder inside a bigger repository.

To try an idea without risking the game, make a variant (`jm variant new
floaty-jump`; `variant_new`): a branch in its own folder that builds and plays
on its own. Work there (`open_game` its dir; cd over the shell), commit, then
`variant_compare` shows what differs and a frame of each at the same moment
(with `play`, the person's inputs in both); `variant_play` lets them feel it.
They choose: `variant_merge` brings it in, `variant_drop` throws it away.
An experiment of your own that may go nowhere (a refactor, a tuning sweep)
goes in a sandbox, never the game: `sandbox_new` (`jm sandbox new`) copies the
game as it is, `sandbox_run` runs jm there with a timeout (`["test",
"--json"]`), `sandbox_promote` keeps one that worked as a variant; the rest
expire on their own.
What's to be done lives on the task board (`jm tasks`; `tasks`, `task_add`,
`task_move`, `task_remove`; design/Tasks.md). When the person points at
something for later (a marker, an entity), `task_add` it with that link
(`jm://play/<id>/m2`, `jm://entity?scene=…&name=…`, a file, a note); move a
task to Doing when you start it and to Done once it's built, played and they
agree.

## The loop

1. **Look things up:**
   - `jm schema` lists every component's keys, with defaults and choices;
     `jm schema SpriteComponent` shows one.
   - The script API is in `jm docs scripting`; its sources (one file per
     area: `entity.ts`, `input.ts`, `state.ts`, ...) are in
     `assets/scripts/node_modules/@jm/runtime/` after the first build.
   - Coming from Godot, Unity, Box2D or Tiled? `jm docs glossary` maps their
     names to Journeyman's and flags the ones that behave differently
     (`scale` is a half size, `onOverlap` fires every frame; `onOverlapStart` once).
2. **Edit** the files.
3. **Build:** `jm build --json`. Each line is a problem, given as
   `level`, `category`, `message`, `file`, `line` and `column`. The last line
   is `{"result": "ok"|"failed", ...}`. Fix every error; warnings about
   scene keys are almost always typos. The first build downloads the script
   compiler if the machine has none, and `jm doctor` shows what it uses.
4. **Test logic:** run `jm test` (`--json` for one JSON line per test). Each exported function in
   `tests/*.spec.ts` is a test, and `assert(cond, "message")` fails it.
   The game's state and its save live in memory there. Rendering and audio
   do nothing.
5. **Play it, headless and step by step** (over MCP: `drive_start`, `drive`,
   `drive_frame` to see it, `drive_stop`; never by clicking the game's
   window). The game only advances when told.
   Each line on stdin is a command, and each answer is one JSON line:
   ```sh
   printf 'step 60\npress Enter\nstep 120\nstate\nquit\n' | JM_DRIVE=1 JM_RENDERER=none jm run
   ```
   - `state` returns every entity with its components, the session state, the
     save, the UI layout and the frame's draw list: big. Ask for parts:
     `state session`, `state ui`, `state tag=Paddle` (entities with that tag),
     several at once (`state session tag=Ball tag=Paddle`), or one component
     (`state tag=Ball TransformComponent`). `get` answers a single value, handy
     in a shell loop: `get tag=Ball TransformComponent.x` gives
     `{"ok":true,"value":-52.4}`, `get session.score` too; `get camera.x` is
     where the camera looks.
   - Wait for something instead of guessing frame counts: `until tag=Lift
     TransformComponent.y < -270 max 600` steps until it's true (or says where
     it got after 600 frames). `echo <text>` labels a transcript.
   - Something should touch and doesn't (a pickup, a landing)? `near tag=Player`
     lists what's within 4 units, with the gaps: a 0.01 miss shows as `"gap": 0.01`.
   - Commands: `step [n]`, `state`, `get`, `until`, `echo`, `near`, `down|up|press <Key>`,
     `set <key> <json>`, `scene <path>`, `capture <file.png>` (needs GL: use
     `JM_HEADLESS=1` instead of `JM_RENDERER=none`), and `quit`.
   - Key names are the same in the driver, replay files and input bindings:
     `A`–`Z`, `Digit0`–`Digit9`, `Space`, `Enter`, `Escape`, `ArrowLeft`
     (the full list is under *Input bindings* in `jm docs content`). An
     unknown name is an error.
   - Errors the game logs come back in the next `step`'s `errors`. Add
     `JM_STRICT=1` to stop on the first one.
6. **Look at it**, if the machine has a display or software GL:
   ```sh
   JM_HEADLESS=1 JM_EXIT_AFTER_FRAMES=300 JM_CAPTURE_DIR=/tmp/f JM_CAPTURE_FRAMES=60,299 jm run
   ```
   Then view the PNGs. Through MCP, `drive_start` with `gl: true` and then
   `drive_frame` returns the driven game's current frame as an image: no
   files to place or clean up. On a Linux machine with no display, software GL is
   two packages away: `apt-get install -y xvfb libgl1-mesa-dri libglx-mesa0`,
   then prefix the command with `xvfb-run -a`. Once a screen looks right, record it with
   `jm golden --update` (`tests/golden/<name>.golden.json` lists the frames, an
   optional replay, and the scene to start in). From then on `jm golden`
   fails if it changes.
7. **Format:** run `jm fmt` before you commit. It writes the JSON layout the
   editor writes, so diffs stay small.

Keep the project to the game. Images you look at, logs and scratch files go
in `.jm/` (it ignores itself in git) or the temp dir, never the project root;
over MCP, `drive_frame` and `play_frame` return images with no file at all.
Reach for jm's own commands (the driver, plays, `jm test`, `jm golden`)
before writing a helper script: a wrapper around them is a second toolchain
the next agent has to learn.

No `jq` or `python`? `jm doctor --json` names the Node that jm uses for
scripts (`toolchain.node`), which can run a small JSON helper. Usually `get`
is enough.

Runs are deterministic: headless runs use a fixed 1/60 s step and seed 1. The
same inputs give the same frames and the same state, so a failure you found
once can be replayed.

If a tool speaks the Model Context Protocol, `jm mcp` serves these same
commands over stdio (`build`, `test`, `golden`, `fmt`, `export`, the driver,
the plays). `jm setup` adds it to the agent apps on this machine (Claude Code,
Claude Desktop, Codex, ChatGPT Desktop: it shares Codex's settings) and
`jm setup --remove` takes it out; without ChatGPT Desktop, `jm setup chatgpt`
prints the steps for ChatGPT on the web. The editor's Agents page does both with a button.
For Codex and ChatGPT Desktop, setup also installs the Journeyman plugin: its
skills (working on a game, the playtest loop, levels and art, design memory,
history, experiments, the task board, onboarding) and
a session-start hook that names the game in the session's folder. A new game
(`jm init`) brings the plugin too, in its own marketplace
(`.agents/plugins/marketplace.json` and `plugins/journeyman`), offered when
Codex opens the game's folder.
The person may make art and notes in other tools: `jm companions` (over MCP,
`companions`; in ChatGPT, Journeyman's settings page) turns on Aseprite
(sprites), Tiled (maps), Obsidian (design notes) or Basic Memory (memory)
for you: each brings a skill on working across it and Journeyman, and its MCP
server where it has one (Aseprite's, Basic Memory's). Never needed, and only when the person asks.
In ChatGPT, Journeyman in the sidebar is Journeyman Studio (the `games` tool
as a page; `studio` opens it in a chat): the person's games to open, play or
export, then tabs on the open game beside the editor (Overview: its scenes and
plays; Editor: what's open there, their pointed-out notes, which also come to
the chat as drafts, picking a scene or entity to show there, and the scene as
the engine draws it). What they pick or look at there comes to you as context; `open_game` a
picked game before working on it. To show them something, call `studio` with
a route: "/" the games, "/game" the overview, "/editor" the editor bridge,
"/files?file=<path>&line=<n>" a file with what's wrong in it,
"/memory?note=Pillars" a design note, "/git?version=<hash>" a version,
"/variants?variant=<name>" a variant to compare, "/tasks" the task board,
`?game=<path>` to open another. "Work here" on a variant or sandbox opens its
folder as the game until "Back to the game": your tools then change that copy,
not the game (`games` lists it with `copyOf`, `copyKind` and `copy`, and the
page's context says so). `scene_view` shows the
person a scene laid out (prefabs applied) and gives you each entity's place and
components; .scene.json files they open in ChatGPT desktop open in it, and the
entity they pick there is your context.
What the person picks on a page (a game, a scene, an entity, a moment of a
play) comes to you as a titled item in their message, with its details; where
they are on the page comes as background context. "Ask about it" and "New
chat" on a page start their message about that thing, here or in a new chat:
look at it (`open_game`, `scene_view`, `play_frame`) before you answer.
When the person @-mentions part of the game (ChatGPT desktop), it arrives as a
resource link: files are `file://` resources, and `jm://play/<id>`,
`jm://play/<id>/m<n>` (a marker, with what they saw; a moment's with what
they picked, outlined, and where each came from) and
`jm://entity?scene=…&name=…` (every entity of that name in the scene) read
the same way.
`jm editor` opens this game in the editor, where your user works on the same files.
Without a shell (ChatGPT), the game's files are tools: `files` (the tree with
git's marks, as `jm files`), `file_read` (text, an etag, and what's wrong: bad
JSON, not in jm fmt's layout, schema problems), `file_write` (pass the etag so
you never save over the person's change), `file_move`, `file_delete`,
`files_search` (as `jm files search`) and `file_open` (their code editor).
A file they attach in chat (a sprite sheet, a .tmj map, a sound) goes into
assets/ with `asset_add`; then use its path in a scene or prefab.

Over MCP, `editor_state` says what they have open and selected there, and the
notes they pointed out to you (Edit > Point Out to Your Agent: "this" is what
they selected); `editor_show` shows them something there (opens the scene,
selects and frames an entity, with your note), opening the editor if needed.
`scene_render` draws a scene in the engine (headless, built first if stale):
to see a change without asking the person to play.
`jm projects` lists the person's games (the ones they opened, newest first, and
the games folder's); over MCP that's `games`, and `project_open_editor`,
`project_play` and `project_export` take any of them.
`settings_read` lists the game's settings (name, start scene, window size,
logical size: .jm.json) and the editor's preferences by key, with their types
and values; `settings_update {set: {"config.window.width": 960}}` changes them
(ChatGPT shows the same fields on Journeyman's settings page). Its Agent
settings are how the person wants you to work, kept in the editor's
preferences: `agent.commitMilestones` (commit after each working change),
`agent.experiments` (risky changes in a sandbox, a variant, or ask),
`agent.recordDecisions` (write decisions into design/) and
`agent.askBeforeRestore`. Read them once and follow them.
When it's unclear which scene, prefab or texture the person means ("this
level", "that sprite"), `pick {kind: "scene", message: "Which level?"}` asks
them in a picker with previews and gives you the paths. Where the host shows
forms, `new_game` with no name and `project_export` with no target let the
person choose (start from, name, picture; platform, signing); give the
arguments yourself to skip the form.

## Platformers and moving things

- **Ground:** draw it in Tiled on an object layer as polylines or polygons of
  class `ground` (solid) or `platform` (one-way), or give a scene entity a
  `GroundComponent` (`jm docs content`, *Drawn ground*).
- **Player:** a `BoxColliderComponent` plus `"VelocityComponent": {"acceleration":
  [0, -900], "motion": "walk"}`. Its script sets `me.velocity.x` and jumps
  when `me.velocity.onGround` (the example under *Moving bodies* in `jm docs scripting`).
  One thing moves a body: don't also call `move()`/`walk()` on it.
- **Lifts and carts:** `"BoxColliderComponent": {"halfExtents": [20, 2], "blocksMask": 1}`
  (solid: `blocksMask` defaults to 0, solid to nothing) plus `"VelocityComponent":
  {"motion": "move"}`: a moving platform. Whatever stands on it moves with it, and what it runs into is
  pushed. Being blocked stops its velocity, so its script sets it each frame.
- **Check it:** `get tag=Player VelocityComponent.blockedY` is -1 while it
  stands; `state tag=Player VelocityComponent` also shows what it stands on
  (its floor: `floorIndex`, `floorGeneration`) and how fast that goes
  (`platformVelocityX`, `platformVelocityY`). To see the ground and colliders, send `debug physics on`, then
  step and `drive_frame` (after `drive_start` with `gl: true`) or `capture`, or
  run with `JM_DEBUG_PHYSICS=1`. To see what a script thinks (a ray, a target,
  a value), have it call `Debug.line`/`circle`/`rect`/`text`: `state debug`
  lists them and captured frames show them.

## When the person points at something

From the editor they paste references: `scenes/level1.scene.json#Hero/Sword` is
an entity (a path of names in that scene; `Bat[2]` the second of that name),
`scenes/level1.scene.json@120,-40` a spot in the world (y up), and `(on the
ground of ...#Map)` says it's on that entity's ground (a map's: the Tiled
objects of class `ground` or `platform` near there). Look there before
guessing: `debug physics on` and a frame show it.

While the editor is open, it and you share the files. If `jm build` (or `jm
doctor`) warns that a file is open in the editor with unsaved edits, ask the
person to save it first. Otherwise a scene or prefab merges entity by entity (an entity
you both changed takes your version) and other files take yours; their edits
are one Undo back.

## When the person has played

The person plays with `jm run`, and each play is recorded (`.jm/plays`). They
press F8 at moments that look wrong, then tell you about it. Look before you
guess: `jm plays show` (scenes, values over time, their markers),
`jm plays frame latest m1` (what they saw at marker 1, replayed exactly),
`jm plays state latest m1 session` (the numbers then). After a fix, `jm plays
verify` says whether their play now goes differently, and `jm plays resume
latest m1` opens the game for them right at that moment to try it. All of it
is in `jm docs plays`. Over MCP, use the tools instead: `play_show`,
`play_frame`, `play_moment`, `play_state`, `play_inputs`, `play_verify`, `play_resume`.

## AssemblyScript gotchas

- Number types are explicit: `i32`, `f32`, `f64`, `u32`. Use `<f32>x` to
  convert. Use `Mathf` for `f32` math; `Math` is `f64`.
- No `any`, no union types, and no `undefined`. Nullable is `T | null` for
  objects only. Closures can't capture local variables, so use module-level
  state or class fields.
- Each scripted entity runs its own instance, so module-level variables are
  that entity's state. Instances are expensive to create: keep scripts off
  bullets and particles, and give those `VelocityComponent` and
  `LifetimeComponent` instead.
- `assert((1 + 1) == 2)` fails: AssemblyScript folds all-literal comparisons
  wrongly inside `assert`. Real values are fine.
- Hooks are all optional: top-level code (setup), `onUpdate(dt: f32)`,
  `onOverlapStart(other: Entity)` / `onOverlapEnd(other: Entity)` (once),
  `onOverlap(other: Entity)` (every frame), `onLanded()` / `onLeftGround()`,
  `onDestroy()` and `onMessage(m: Message)`.

## Reference

`jm docs` lists the topics, and `jm docs <topic>` prints one:

| Topic | Covers |
|---|---|
| `scripting` | the `@jm/runtime` API |
| `glossary` | Godot, Unity, Box2D and Tiled names, and what differs |
| `content` | every file format and component |
| `runtime-gameplay` | building blocks: menus, timers, projectiles, HUDs |
| `testing` | every `JM_*` variable, the driver, dumps and goldens |
| `networking` | multiplayer: sessions, shared entities, servers, `jm run --peers` |
| `plays` | the person's recorded plays: markers, replays, `jm plays`, Codex and ChatGPT |
| `editor` | the visual editor a person may open on the same files |
