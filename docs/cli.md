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
build for a newer macOS or a quarantine), and the script toolchain (Node.js and AssemblyScript: the
machine's or the project's own, else the copies jm downloads to ~/.jm/toolchains).

--fetch downloads whatever of the toolchain is missing now, rather than on the
first build (for an image or a CI cache). --json prints one JSON object:
{"ok", "jm", "engine", "toolchain", "project", "agents", "problems"}. Exits 1 when
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

## jm export

Build a standalone game: one executable with everything inside.

Builds the project and appends its archive to a copy of the engine (the
"player"), giving one self-contained executable:

```text
  macOS:    dist/<Name>.app    (its executable carries the game; --bare for just the binary)
  Linux:    dist/<Name>
  Windows:  dist/<Name>.exe
```

The result runs without the CLI, Node, the project sources or any data files.

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
      --bare            macOS: write the executable alone, not an .app
      --out string      Output directory (default "dist")
      --player string   Engine executable for the target platform
      --server          Export the dedicated multiplayer server (journeyman_server)
      --skip-build      Export the existing build/ without rebuilding
      --target string   Platform as os-arch (default: this machine)
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

Creates .jm.json, scenes/main.scene.json, the scripts folder, AGENTS.md and
CLAUDE.md in the current directory (not a new folder: mkdir it and cd in first),
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
games folder: $JM_GAMES, else ~/Journeyman.

```text
Usage: jm mcp [flags]

Flags:
      --allow-origin stringArray   with --http, also answer requests from this origin host (default: only this machine's)
      --dir string                 the game's folder (default: the current one), for clients that start servers elsewhere
      --http string                serve over HTTP at this address instead (e.g. "127.0.0.1:8787"): for ChatGPT apps; prints the URL to use
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

```text
The engine's JM_* variables pass through; the ones for unattended runs:
  JM_DRIVE=1            stepped by commands on stdin, one JSON answer per line
                        (step [n], state [part...] [tag=Name], get <path>, until <path> <op> <value>, near tag=Name, press <Key>, quit)
  JM_RENDERER=none      no window or GL: runs with no display (a container)
  JM_HEADLESS=1         a hidden window, with GL: frames can be captured
  JM_STRICT=1           the first error ends the run with exit code 1
  JM_EXIT_AFTER_FRAMES=n, JM_CAPTURE_DIR + JM_CAPTURE_FRAMES, JM_DUMP_DIR,
  JM_INPUT_REPLAY, JM_ERRORS, JM_SEED ...: jm docs testing has them all.
A windowed run with JM_EXIT_AFTER_FRAMES (not driven) still going 30 s past
three times its frames' time is stopped: a sandbox blocking the window server
hangs it.
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
ChatGPT reaches MCP servers over the internet, so for it setup prints the steps.

```text
Usage: jm setup [agent...]
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
