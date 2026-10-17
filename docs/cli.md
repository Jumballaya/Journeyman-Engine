# The jm CLI

Every command's `--help`, generated from jm itself (`jm docs cli`). `jm <command> --help` prints the same.

## jm build

Build game assets and compile AssemblyScript.

Builds the project into build/: compiles scripts, bakes atlases and
tilesets, and checks scenes and prefabs against the engine's schema.

--json prints no progress; each problem is a JSON line ({"level", "category",
"message", "file", "line", "column"}) and the last line is the result
({"result": "ok"|"failed", "errors", "warnings"}).

```text
Usage: jm build [flags]

Flags:
      --json   problems as JSON lines, no progress (for tools)
```

## jm companions

Tools beside Journeyman (Aseprite, Tiled, Obsidian, Basic Memory): turn them on for your agent.

Companions are tools people make games with beside Journeyman. None is
needed, and jm installs none. Turning one on adds to Journeyman's agent
plugin (jm setup codex or chatgpt installs it) a skill that teaches the work
across it and Journeyman, and its MCP server where it has one:

```text
  aseprite      draw and animate sprites (MCP: diivi/aseprite-mcp, run with uv)
  tiled         paint tile maps (a skill: the game reads Tiled's files)
  obsidian      the game's design/ notes as a vault (a skill: notes are files)
  basic-memory  remembers design and findings across chats (MCP: basic-memory)
```

```text
  jm companions                  each one: on or off, and whether it's here
  jm companions enable <name>    turn one on
  jm companions disable <name>   turn it off
```

```text
Usage: jm companions
```

## jm companions disable

Turn a companion off.



```text
Usage: jm companions disable <name>
```

## jm companions enable

Turn a companion on.



```text
Usage: jm companions enable <name>
```

## jm docs

Print the engine's guides (scripting API, formats, testing, ...).

Without a topic, lists the guides; with one, prints it as markdown. They're
built into jm, so they match this version and need no network. --json lists
the topics as JSON.

```text
Usage: jm docs [topic] [flags]

Flags:
      --json   list the topics as JSON
```

## jm doctor

Check that jm can build and run games here.

Checks this machine (and the project in the current folder, if any): jm's
version, the engine it would run, whether it starts and matches, the engine's
schema, the install (jm first on PATH, the server beside it; on macOS, a
build for a newer macOS or a quarantine), the keychain's signing identities
(macOS: what jm export --sign takes), and the script toolchain (Node.js and AssemblyScript: the
machine's or the project's own, else the copies jm downloads to ~/.jm/toolchains).

--fetch downloads whatever of the toolchain is missing now, rather than on the
first build (for an image or a CI cache). --json prints one JSON object:
{"ok", "jm", "engine", "toolchain", "project", "agents", "problems", "signing"}. Exits 1 when
something would stop a build or a run.

```text
Usage: jm doctor [flags]

Flags:
      --fetch   download the missing toolchain now
      --json    one JSON object (for tools)
```

## jm editor

Open a game in the editor (default: the game in this folder).

Starts the Journeyman editor on a game: the folder given, else the current
one if it's a game, else the editor's start screen. It returns once the editor
is up (an error if it quits as it starts); the editor runs on its own.

The editor is $JM_EDITOR, else the one beside jm (the editor's download has
jm inside), else the one install.sh --editor put in ~/Applications (macOS) or
~/.jm/editor (Linux, Windows).

```text
Usage: jm editor [folder]
```

## jm examples

The example games: list them, or start a game from one.

The example games of this jm's release (the repo's demos/): the first use
downloads them (about 15 MB) into ~/.jm/examples/\<version>.

```text
  jm examples                      the examples
  jm examples copy <name> [dir]    a new game in dir (default: ./<name>) from one,
                                   yours to change; then jm build && jm run
```

```text
Usage: jm examples

Flags:
      --json   JSON output (for tools)
```

## jm examples copy

Start a new game from an example.



```text
Usage: jm examples copy <name> [dir]
```

## jm examples list

The examples.



```text
Usage: jm examples list
```

## jm export

Build a standalone game: one app or executable with everything inside.

Builds the project and packs it with a copy of the engine (the "player"):

```text
  macOS:    dist/<Name>.app    (the game in Contents/Resources/game.jm; --bare for one binary)
  Linux:    dist/<Name>        (the game appended to the executable)
  Windows:  dist/<Name>.exe
```

The result runs without the CLI, Node, the project sources or any data files.

macOS exports are signed ad hoc, which runs here but not on other Macs
downloaded from the web. --sign \<identity> signs with a Developer ID
Application identity from the keychain ("auto": the only one there; jm doctor
lists them), with the hardened runtime and a timestamp. --notarize \<profile>
then sends the .app to Apple with that notarytool keychain profile (make it
once: xcrun notarytool store-credentials \<profile>), waits for the verdict
(often minutes) and staples it, so it opens on any Mac. --notarize alone means
--sign auto. jm never sees a password or key: only these keychain names.

--server exports the game's dedicated multiplayer server instead: the same
game files appended to journeyman_server (the engine without its window,
renderer, UI and audio), leaving out images, sounds, UI, shaders and fonts.
It's written as dist/\<Name>-server[.exe] and listens on .jm.json's net.port
(JM_NET_PORT overrides). Its scene is net.server.entryScene, if set.

--target os-arch (e.g. linux-amd64, windows-amd64, darwin-arm64) exports for
another platform, using that platform's player: --player \<path>, or
players/\<target>/journeyman_engine[.exe] next to jm or in $JM_PLAYERS.
Manifest settings under config.export: "icon" (a PNG, macOS) and "bundleId".

```text
Usage: jm export [flags]

Flags:
      --bare              macOS: write the executable alone, not an .app
      --notarize string   macOS: notarize and staple the .app with this notarytool keychain profile
      --out string        Output directory (default "dist")
      --player string     Engine executable for the target platform
      --server            Export the dedicated multiplayer server (journeyman_server)
      --sign string       macOS: sign with this keychain identity ("auto": the one Developer ID)
      --skip-build        Export the existing build/ without rebuilding
      --target string     Platform as os-arch (default: this machine)
```

## jm files

The game's files, with what git says changed.

Lists the game's files (not .git, .jm, build, dist or node_modules) with
their sizes and git's mark for each changed one: M changed, A added, ? new
to git, D deleted, U in conflict.

```text
  jm files                  the list (--json for tools)
  jm files search <text>    files whose name or lines hold text (any case)
```

```text
Usage: jm files

Flags:
      --json   JSON output (for tools)
```

## jm files search

Files whose name or lines hold text (any case).



```text
Usage: jm files search <text>
```

## jm fmt

Write the project's JSON in the shared layout.

Rewrites JSON files in the layout the editor and jm both write: two-space
indent, keys in their order, short arrays of scalars on one line, whole
numbers as integers. A file reads the same whoever wrote it, and a small
change is a small diff.

With no files: .jm.json, scenes, and the .json files under assets/ (not
npm's files, builds, or Tiled's .tmj/.tsj). --check changes nothing and
fails if a file isn't in the layout (for CI).

```text
Usage: jm fmt [files...] [flags]

Flags:
      --check   change nothing; fail if a file needs formatting
```

## jm generate

Scaffold a new script, prefab, scene, ui screen, shader, or bindings file.

Create empty source files for the project. Run `jm generate list` to see what can be generated.

```text
Usage: jm generate
```

## jm generate bindings

Generate a new bindings.



```text
Usage: jm generate bindings <name>
```

## jm generate list

List the kinds of files `jm generate` can create.



```text
Usage: jm generate list
```

## jm generate prefab

Generate a new prefab.



```text
Usage: jm generate prefab <name>
```

## jm generate scene

Generate a new scene.



```text
Usage: jm generate scene <name>
```

## jm generate script

Generate a new script.



```text
Usage: jm generate script <name>
```

## jm generate shader

Generate a new shader.



```text
Usage: jm generate shader <name>
```

## jm generate ui

Generate a new ui.



```text
Usage: jm generate ui <name>
```

## jm golden

Compare the game's frames with recorded golden images.

Plays each tests/golden/\<name>.golden.json headless from the build and
compares the frames it lists with tests/golden/\<name>/frame_NNNNN.png:

```text
  {"replay": "boss.replay.txt", "scene": "scenes/boss.scene.json",
   "session": {"lives": 1}, "frames": [60, 300]}
```

replay (inputs, as JM_INPUT_REPLAY), scene and session (a deep link: start
there, with that state) are optional. A frame passes when at most maxDiff
(default 0.5%) of its pixels differ by more than threshold (default 40 of
255) in a channel, so different GPUs agree; frames are compared at the
window's size, which headless runs render at on every machine. A failing frame
leaves its capture and a diff image in build/golden/\<name>/. The run is
strict: an error the game logs fails it too.

--update records the images instead (after you've looked at them). Run
jm build first. --json reports like jm build --json.

```text
Usage: jm golden [name...] [flags]

Flags:
      --json     problems as JSON lines, no progress (for tools)
      --update   record the frames as the new golden images
```

## jm init

Bootstrap a new Journeyman project in the current directory.

Creates .jm.json, scenes/main.scene.json (a white square the arrow keys move,
by assets/scripts/player.ts), the scripts folder, design/ (the game's design
notes: jm memory), AGENTS.md and CLAUDE.md in the current directory (not a new folder: mkdir it and cd in first),
ensures build/ + *.jm are gitignored, and runs git init when git is installed
and the folder isn't already in a repository.

[name] is the game's name; if omitted, the directory's basename.
Refuses to run if .jm.json already exists.

```text
Usage: jm init [name]
```

## jm mcp

Serve this CLI over MCP (stdio), for agents.

Speaks the Model Context Protocol on stdin/stdout (JSON-RPC, a message
per line), from the project in the current folder.

Tools run jm's own commands: build, test, golden, schema, generate, and
drive_start / drive / drive_stop for the engine's stepped driver (JM_DRIVE:
one game at a time, a command per call, e.g. "step 60", "press Enter",
"state"). Resources are the project's files (.jm.json, scenes, prefabs,
scripts, UI, data) and jm://schema.

Register it with an MCP client as the command "jm mcp" (jm setup does it),
run in the project. Started anywhere else (Claude Desktop starts it in no
folder), the games / new_game / open_game tools make and open games in the
games folder: $JM_GAMES, else ~/Journeyman. games lists those and every game
the person opened (jm projects); project_open_editor, project_play and
project_export act on any of them without opening it here.

For hosts without a shell (ChatGPT), the open game's files are tools too:
files (jm files: the tree, git's marks), file_read and file_write (an etag
keeps a save from going over someone's change; jm's JSON is saved in jm fmt's
layout), file_move, file_delete, files_search (jm files search) and file_open
(the file in the person's code editor). The game's design notes (design/,
jm memory) are memory, memory_search, memory_note and memory_decide, and the
prompt new_game_interview plans a game with the person into them.

editor_state and editor_show bridge to the editor on the person's screen:
what they have open and selected and the notes they point out to their agent
there (Edit > Point Out to Your Agent), and showing them a scene or an
entity there (opened, selected, framed, with a note). scene_render draws a
scene in the engine, headless. play_live steers the game the person is
playing (pause, step, slow, watch, set, mark, profile, a picture).
game_index links the game's files (uses, used by, unused); health runs
doctor, build, tests and goldens in one answer.

Hosts that show apps (ChatGPT, Claude) get pages too: games and studio are
Journeyman Studio (ChatGPT's sidebar and a chat's tabs: the games to open,
play, export or make, then the open game's tabs beside the editor; studio's
route shows a page, e.g. "/game"), play_show the Play review timeline, and
scene_view a scene laid out with an inspector (ChatGPT desktop opens
.scene.json files in it and saves changes to the file).

settings_read and settings_update are the open game's settings (name, start
scene, window, logical size in .jm.json) and the editor's preferences as typed
fields by key: ChatGPT shows them on Journeyman's settings page.

Hosts that show forms ask the person instead of guessing: new_game with no
name (what to start from, the name, the picture's shape), project_export with
no target (platform, server, Mac signing) and pick (the game's scenes,
prefabs, textures... in a picker with previews). Clients of the 2026-07-28
protocol get them as input_required results (any transport); older ones as
requests to the client, over stdio.

```text
Usage: jm mcp [flags]

Flags:
      --allow-origin stringArray   with --http, also answer requests from this origin host (default: only this machine's)
      --dir string                 the game's folder (default: the current one), for clients that start servers elsewhere
      --http string                serve over HTTP at this address instead (e.g. "127.0.0.1:8787"): for ChatGPT apps; prints the URL to use
```

## jm memory

The game's design notes and decisions (design/).

The game's memory: design/, Markdown notes on what the game is and why
(Pitch, Pillars, Levels, Systems, Decisions, Notes), for the person and their
agent. Obsidian opens design/ as a vault; the notes are also Basic Memory's
format (a note per thing, "- [category] text" observations, "- kind [[Note]]"
relations), so it can index them too. [[Note]] links a note; a game file is
named in backticks (`scenes/main.scene.json`).

```text
  jm memory                         the notes (--json for tools)
  jm memory init                    make design/ (jm init does, for new games)
  jm memory show <note>             a note, what it links and what links it
  jm memory search <text>           notes whose title or lines hold text
  jm memory note <text> [--in Levels] [--category feel]
                                    add an observation (default: to Notes)
  jm memory decide <title> <why>    record a decision (design/decisions/)
```

```text
Usage: jm memory

Flags:
      --json   JSON output (for tools)
```

## jm memory decide

Record a decision and why (design/decisions/).



```text
Usage: jm memory decide <title> <why>
```

## jm memory init

Make design/ (kept where it exists).



```text
Usage: jm memory init
```

## jm memory note

Add an observation to a note (default: Notes).



```text
Usage: jm memory note <text> [flags]

Flags:
      --category string   what kind of observation, e.g. feel, idea, bug, playtest (default "note")
      --in string         the note to add to (made if there's none of that name) (default "Notes")
```

## jm memory search

Notes whose title or lines hold text (any case).



```text
Usage: jm memory search <text>
```

## jm memory show

A note, what it links and what links it.



```text
Usage: jm memory show <note>
```

## jm migrate

Rewrite a Journeyman project to source-only layout (.ts replaces .script.json).

Walks the manifest's assets[] for legacy .script.json entries, rewrites them to the
underlying .ts paths, rewrites scene script references, deletes the .script.json
files, and ensures build/ + *.jm are gitignored.

Idempotent: a second run prints "nothing to migrate" and exits 0.

```text
Usage: jm migrate [flags]

Flags:
      --dry-run   Print planned changes without writing
      --force     Skip the clean-git-tree check
```

## jm pack

Pack a built game into a single .jm archive.



```text
Usage: jm pack [build-dir] [flags]

Flags:
      --out string   Output archive path (default: build/<slug>.jm)
      --server       For a dedicated server: leave out images, sounds, UI, shaders and fonts
      --strict       Error on any unrecognized file
```

## jm plays

The plays `jm run` recorded: list, look at, replay and resume them.

Every time you play with jm run, the play is recorded in .jm/plays/\<id>:
your inputs and frame timing (enough to replay it exactly), the state every
30 frames, a thumbnail every 60, and the moments you marked with F8.
Your agent can then see what you saw, at the moment you mean.

A play is named by its id, a unique start of it, "latest" (the default),
or "latest-1", "latest-2" for the ones before. A moment in it is a frame ("420"), a time
("12.5s", "1:05"), a marker ("marker:2" or "m2"), "start" or "end".

```text
  jm plays                         the plays, newest first
  jm plays show [play]             what happened: scenes, values over time, markers
  jm plays state [play] [moment]   the game's state then: all but the draw list,
                                   or the driver's parts (session, ui, draw, tag=Name, ...)
  jm plays frame [play] [moment]   an image of that moment
  jm plays inputs [play] [from] [to]  what was pressed, and for how long
  jm plays drive [play] [moment]   the driver (JM_DRIVE), starting at that moment
  jm plays resume [play] [moment]  play on from that moment yourself
  jm plays verify [play]           does it still replay the same (after a change)?
  jm plays prune --keep N          delete all but the newest N
```

jm run keeps the newest 40 plays, and every play with a marker.

Replays need the build the play was made with: after changing the game, a
replay may go differently (that's verify's question). --json everywhere.

```text
Usage: jm plays

Flags:
      --json   JSON output (for tools)
```

## jm plays drive

The stepped driver, starting at a moment of the play.



```text
Usage: jm plays drive [play] [moment]
```

## jm plays frame

An image of a moment (replayed with GL, else the nearest thumbnail).



```text
Usage: jm plays frame [play] [moment] [flags]

Flags:
      --out string   where to write the PNG (default: the play's folder)
```

## jm plays inputs

What the person pressed, and how long (default: the whole play).



```text
Usage: jm plays inputs [play] [from] [to]
```

## jm plays list

The recorded plays, newest first.



```text
Usage: jm plays list
```

## jm plays prune

Delete all but the newest plays (marked ones too).



```text
Usage: jm plays prune [flags]

Flags:
      --keep int   how many to keep (default 40)
```

## jm plays resume

Play on yourself from a moment (recorded as a new play).



```text
Usage: jm plays resume [play] [moment]
```

## jm plays show

What happened in a play.



```text
Usage: jm plays show [play]
```

## jm plays state

The game's state at a moment (replayed).



```text
Usage: jm plays state [play] [moment] [part...]
```

## jm plays verify

Replay a play to its end: does it go the same way?.



```text
Usage: jm plays verify [play]
```

## jm projects

Your games: the ones you opened (the editor's recent projects) and the games folder's.

Lists your games, newest opened first: the editor's recent projects (jm
and its MCP tools keep the same list), then the games folder's others
($JM_GAMES, else ~/Journeyman).

```text
  jm projects                 the list (--json for tools)
  jm projects add <folder>    put a game on the list
  jm projects forget <folder> take one off it (its files stay)
  jm projects scan [folder]   add every game in a folder and the folders below it (default: this one)
```

```text
Usage: jm projects

Flags:
      --json   JSON output (for tools)
```

## jm projects add

Put a game on the list.



```text
Usage: jm projects add <folder>
```

## jm projects forget

Take a game off the list (its files stay).



```text
Usage: jm projects forget <folder>
```

## jm projects list

The list.



```text
Usage: jm projects list
```

## jm projects scan

Add every game in a folder and the folders below it.



```text
Usage: jm projects scan [folder]
```

## jm run

Run the Journeyman game engine (default: ./build).

Runs the game (default: ./build).

When you play a project's build, the play is recorded in .jm/plays (F8 marks
a moment): jm plays shows them, and your agent can replay them exactly.
--no-record skips it; driven, replayed and headless runs never record.

```text
Multiplayer (.jm.json "net"; see docs/networking.md):
  --server       run the dedicated server (journeyman_server) instead of the game
  --host         the game hosts a session; --join host:port joins one
  --peers N      a whole session on this machine: with net.topology "server",
                 a server and N games joining it; with "p2p", N games, the
                 first hosting. Each game gets its own save folder, and
                 output folders and files named in JM_CAPTURE_DIR, JM_DUMP_DIR,
                 JM_NET_TRACE and JM_ERRORS get a per-peer suffix (peer1...).
                 JM_INPUT_REPLAY may say {peer}: replay.{peer}.txt.
  --port P       the session's UDP port (default: net.port, else 7777)
  --latency MS   simulated network trouble: each message held MS milliseconds,
  --loss P       and unreliable ones (positions) dropped with probability P (0..1)
```

--profile shows, when the game quits, where its last frames' time went (per
system, script and module, with entity counts and draw calls).

```text
The engine's JM_* variables pass through; the ones for unattended runs:
  JM_DRIVE=1            stepped by commands on stdin, one JSON answer per line
                        (step [n], state [part...] [tag=Name], get <path>, until <path> <op> <value>, near tag=Name, press <Key>, quit)
  JM_RENDERER=none      no window or GL: runs with no display (a container)
  JM_HEADLESS=1         a hidden window, with GL: frames can be captured
  JM_STRICT=1           the first error ends the run with exit code 1
  JM_EXIT_AFTER_FRAMES=n, JM_CAPTURE_DIR + JM_CAPTURE_FRAMES, JM_DUMP_DIR,
  JM_INPUT_REPLAY, JM_ERRORS, JM_SEED ...: jm docs testing has them all.
A windowed run with JM_EXIT_AFTER_FRAMES (not driven, without --peers) still
going 30 s plus 0.1 s a frame (3x JM_FIXED_DT if longer) later is stopped: a
sandbox blocking the window server hangs it.
```

```text
  printf 'step 60\npress Enter\nstep 60\nstate session\nquit\n' | JM_DRIVE=1 JM_RENDERER=none jm run
```

```text
Usage: jm run [build path or .jm archive] [flags]

Flags:
      --host          Host a multiplayer session
      --join string   Join the multiplayer session at host:port
      --latency int   Simulated latency for every message, in milliseconds
      --loss float    Simulated loss of unreliable messages, 0..1
      --no-record     Don't record this play (jm plays)
      --peers int     Run a whole multiplayer session here: N games (and a server)
      --port int      The session's UDP port
      --profile       When the game quits, show where its last frames' time went: per system, script and module, entities and draw calls
      --server        Run the dedicated multiplayer server
      --watch         Rebuild when the project's files change; the game reloads changed images, atlases, shaders, sounds and scripts as it runs
```

## jm schema

Print the engine's component schema as JSON.

Prints every component the engine knows, as JSON: each one's scene/prefab
keys (kind, default, hint, choices, accepted asset types) and the fields
scripts can reach. With a component name, just that one.

The engine is $JM_ENGINE, else journeyman_engine beside jm or on $PATH;
--engine picks one. jm build checks scenes and prefabs
against this schema.

Script fields are the engine's own names; scripts reach them through
@jm/runtime's wrappers, which may name them differently (VelocityComponent's
vx is entity.velocity.x): see jm docs scripting.

```text
Usage: jm schema [component] [flags]

Flags:
      --engine string   the engine binary to ask (default: $JM_ENGINE, else beside jm, else PATH)
```

## jm setup

Connect agent apps to jm (adds jm's MCP server to their settings).

Adds jm's MCP server ("jm mcp") to agent apps' settings, so they can build,
test and play your games. Agents: claude-code, claude-desktop, codex, chatgpt.
Without one, it sets up every agent app it finds on this machine.

Running it again is safe: it replaces its own entry (named "journeyman") and
leaves the app's other settings alone. Agent sessions already open get the
tools when they restart.
ChatGPT Desktop reads its MCP servers from Codex's settings (~/.codex/config.toml),
so setting up either sets up both. For those two it also installs the
Journeyman plugin (~/.codex/plugins/journeyman, listed in
~/.agents/plugins/marketplace.json): jm's skills, a setup the app offers after
installing, and a hook that tells a session started in a game's folder which
game it is. ChatGPT on the web reaches MCP servers over
the internet: without ChatGPT Desktop, setup chatgpt prints the steps.

--remove takes jm's entry out again (without agents: from every app that has
one); an entry called journeyman that isn't a jm stays.

```text
Usage: jm setup [agent...] [flags]

Flags:
      --remove   take jm out of the agent apps instead
```

## jm test

Run the project's script tests (tests/*.spec.ts).

Compiles each spec with the project's AssemblyScript and runs every exported
function as a test; a failed assert fails it. GameState and Save are in-memory
and Data reads the project's files; other engine calls do nothing, so test
game logic (rules, data, state).
Needs no prior jm build.

--json prints a JSON line per test ({"test", "result": "pass"}, or an error
with "test", "message", "file", "line", "column") and a last line
{"result": "ok"|"failed", "passed", "failed"}.

```text
Usage: jm test [spec.ts ...] [flags]

Flags:
      --json   a JSON line per test, then the result (for tools)
```
