# Plays: you play, your agent sees it

You and your agent build the game together: it edits and builds, you play
and say what feels wrong. A play is how what you saw reaches the agent.

Every time you play with `jm run`, the play is recorded in `.jm/plays/<id>`.
Press **F8** when something looks off: the game holds still ("Paused · F8"),
and you can point at what you mean. Click an entity to pick it, shift-click to
add or take away more; `.` steps a frame. **Enter** saves the moment: a marker
with what you picked (nothing picked: a plain marker; the window's title says
"marker 1 saved"). F8 or Esc goes on without saving. While held, nothing in the
game moves, plays sound or hears your keys, and the held time isn't in the
play: it replays as if you never stopped. Then tell your agent about it, in your own words:
"at my marker the jump felt floaty", "this enemy shouldn't be here".
It replays your play to that moment, exactly, and looks.

A play recorded while files reload (the editor's Play, as you save) ends at the
first reload: a replay needs the files the play ran with.

## What a play holds

| | |
|---|---|
| your inputs and each frame's timing, the seed and the save you started from | enough to replay the play exactly, frame for frame |
| the game's state every 30 frames (half a second at 60 fps) | the scenes you went through and the values that changed (score, lives, ...) |
| a thumbnail every 60 frames, and a screenshot and the state at each marker | what you saw |

Replays are exact because the game is deterministic: the same seed, save,
inputs and frame times give the same game. A replay checks itself against
the recording and says if it ever goes differently, which happens after the
game changes: a play shows what your changed game does with the same hands
on the keys. Gamepads aren't recorded: a play that used one says so, and
`jm plays verify` doesn't check it. Keys, the mouse and the wheel are.

Multiplayer runs (`--host`, `--join`, `--peers`) aren't recorded, nor are
driven (unless asked: below), replayed and headless ones; `jm run --no-record` skips one. Plays live
in `.jm/`, which ignores itself in git; `jm run`
keeps the newest 40, and every play with a marker (`jm plays prune` clears
those too). A minute of play is about 3 MB, mostly thumbnails, and recording
costs the game under a tenth of a millisecond a frame.

## For the agent

```sh
jm plays                         # the plays, newest first
jm plays show [play]             # what happened: scenes, values over time, markers
jm plays frame [play] [moment]   # an image of that moment (replayed exactly)
jm plays state [play] [moment] [part...]  # the state then: entities, session, UI
jm plays inputs [play] [from] [to]  # what was pressed, when, for how long
jm plays drive [play] [moment]   # the stepped driver, starting at that moment
jm plays verify [play]           # does it still replay the same?
jm plays resume [play] [moment]  # the person plays on from there (a new play)
```

A play is its id, a unique start of one, `latest` (the default), or
`latest-1`, `latest-2` for earlier ones. A moment is a frame (`420`), a time (`12.5s`, `1:05`),
a marker (`marker:2`, `m2`), `start` or `end`. Everything takes `--json`.
State parts work as in the driver: `jm plays state latest m1 session` or
`jm plays state latest 1:05 tag=Player TransformComponent`.

`show` lists what was pressed in the 2 s before each marker; `inputs` gives
any stretch ("Space 0:12.3–0:12.7 (0.42 s)"): how long a jump was held is
often the answer to "it felt floaty".

`frame` replays with OpenGL where it can draw (a desktop, or `xvfb-run` on
Linux); where it can't, it gives the nearest thumbnail and says so.

A good loop: the person marks a moment and says what's wrong → `jm plays
show` and `jm plays frame latest m1` to see it → `jm plays state latest m1`
for the numbers → fix the game, `jm build` → `jm plays verify` to see the
play now goes differently, `jm plays frame latest m1` again to see the
difference → `jm plays resume latest m1` so the person tries the fix right
where they were.

## Moments: what you picked

A marker you saved with picks is a moment, and your agent sees what you
pointed at: each pick numbered (`picked 1: Pip [58 0]: scene entry 58 in
scenes/level.scene.json (prefab assets/prefabs/pip.prefab.json)`, or
`spawned from prefab ... by script ... at frame 1021` for one a script made),
which is the file it changes. `play_moment` gives it the picture you saw
with each pick outlined, numbered and named (drawn from where each was, kept
in the pick as `rect`); `play_state` at the moment's frame has the rest. In
ChatGPT the timeline shows a moment's picture outlined and its picks with
where each came from, and "New chat" starts one about them; Studio's
Overview lists the latest moments. A moment can be @-mentioned ("moment 1"),
and comes with its picks and picture.

## In Codex, Claude Code and ChatGPT

`jm mcp` serves the same as tools: `plays_list`, `play_show`, `play_frame`
(the image itself), `play_moment`, `play_state`, `play_inputs`, `play_verify`, `play_resume`, and
`drive_start` with a `play` and `at` to drive on from a moment. To follow
something frame by frame from there, `drive` takes several commands and a
repeat: `{"commands": ["step 1", "get tag=Player TransformComponent.y"],
"repeat": 30}` is one call, and `drive_frame` shows the game as it is then
(start it with `gl: true`).

The agent can make plays too: `drive_start` with `record: true` (and
`seed` to pick the run's randomness, `visible: true` to show it in a window
so you can watch) records what it drives, and `drive_stop` gives the play's
id. Its plays are like yours: the timeline, `play_frame` and `play_verify`
work the same, so "play a minute of level 2, then show me" is tools alone.

The tools that only look say so, so the agent
doesn't ask you before each one; `play_resume` (it opens the game for you),
`build` and `drive` do ask, where your agent asks for anything.

**Codex, Claude Code, Claude Desktop, ChatGPT Desktop:** `jm setup` adds jm's
MCP server to each one it finds (or name one: `jm setup chatgpt`). ChatGPT
Desktop reads its MCP servers from Codex's settings (`~/.codex/config.toml`),
so setting up one sets up both; restart ChatGPT after. The game is the folder
the agent runs in; started elsewhere, `games` and `open_game` pick one.

**ChatGPT on the web** (the same timeline you scrub, ask about a moment from,
and play on from): it talks to MCP servers over HTTPS, so serve jm over HTTP
and put a tunnel in front of it:

```sh
jm mcp --http 127.0.0.1:8787              # in the game's folder
cloudflared tunnel --url http://127.0.0.1:8787   # or: ngrok http 8787
```

`jm mcp` prints its path, `/mcp/<secret>`, new each time it starts. Then in
ChatGPT, with developer mode on (Settings → Apps & Connectors → Advanced),
create a connector with the tunnel's address plus that path. Ask it to show
your latest play: `play_show` opens the timeline. The game and its plays stay
on your machine; the tunnel is only open while it runs.

In ChatGPT the timeline is also **Play review**, a tab you open in any chat
(no question needed): it shows your latest play, and the menu at its top
picks another. What you're looking at is the agent's context for your next
message (the picture, and the moment's frame, scene and values), and **Ask
about this** puts the moment in your draft as an attachment, for you to say
what you mean. Hosts without drafts (Claude) send a question instead.

Type @ in ChatGPT desktop's composer to name part of the game in your
message: a play, a marker ("marker 2: too floaty"), a scene, prefab, script
or entity. The agent gets it as a link it reads: a marker comes with what you
saw there.

The secret path is the password: anyone with the full URL can run your game's
tools, so share it only with the client. Requests from web pages are refused
(only pages on this machine, or an origin named with `--allow-origin`, may
call it).

## Under the hood

A play's folder: `session.json` (game, seed, entry scene, starting session
values, markers (a moment's has its `picks`: each entity's id, `origin`, `rect` (where it was in the picture: x, y, w, h as fractions from its top-left),
`tags` and components then), how it ended: `quit`, `crashed`, or `running` while it
goes), `frames.bin` (each frame's dt: the fixed step, the same at any display
rate; float32), `inputs.jsonl` (each input with its frame: keys by name, so a play
replays on another machine; the pointer; window size and focus; a driver's
`set` and `scene`), `timeline.jsonl` (every 30 frames: scene, session values,
entity count, hashes of the entities and the session values: what a replay
is checked against),
`save.json` (the save it started from), `thumbs/` and `markers/`, and
`jm.json` (two fingerprints of the build it was made with: `build`, what
decides how the game plays, and `look`, everything it draws). After a change
to how the game plays, a replay is the current build's, not what the player
saw; after a change only to how it looks, it plays the same but is drawn
anew. Replayed frames are kept in `frames/`, per build and engine.

The engine does the recording and replaying: `JM_RECORD_DIR` records,
`JM_PLAY_SESSION` replays (with a temporary copy of the save, never the
player's), `JM_PLAY_UNTIL` stops at a frame and `JM_PLAY_THEN=live` hands over
to the player there (fast-forwarding to it, with nothing drawn or heard).
`jm docs testing` lists them with the other `JM_*` variables. The driver's
ready line says which play format the engine speaks (`"plays"`): jm won't
replay or record with an engine older than its plays.
