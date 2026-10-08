# Multiplayer

A Journeyman game becomes multiplayer by sharing some of its entities. One
process **hosts** a session and the others **join** it. An entity with a
`NetworkComponent` (`"Network"` in the editor) is shared: one process simulates
it, meaning it runs its script and decides its state, and streams that state to
everyone else. The other processes show copies that follow it smoothly.
Everything else (HUDs, menus, effects, scenery) stays local to each machine.

The three multiplayer demos each show one way to use this:

| Demo | Topology | What it shows |
|---|---|---|
| [pellet_party](../demos/pellet_party/) | peer to peer, 2–4 | Players simulate their own avatars, so there's no input lag. The host runs the round, and a peer takes over if the host leaves. |
| [tank_arena](../demos/tank_arena/) | dedicated server, up to 8 | The server simulates every tank from its player's input. Rules and bots run only on the server. |
| [checkers](../demos/checkers/) | matchmaker, then peer to peer | A dedicated matchmaker pairs two players, who then connect directly (NAT punching). The host judges a shared board. |

Try one with `jm run --peers 2`, which runs the whole session on this machine.

## Setting it up: `.jm.json`

```json
"net": {
  "topology": "server",
  "port": 7777,
  "maxPlayers": 8,
  "playerPrefab": "player",
  "sendRate": 30,
  "interpolationDelay": 0.1,
  "server": {
    "entryScene": "scenes/arena.scene.json",
    "scripts": ["assets/scripts/server/rules.ts"]
  }
}
```

| Key | Default | Meaning |
|---|---|---|
| `topology` | `"server"` | `"server"`: everyone talks through the host (client/server). `"p2p"`: players also connect to each other directly (a mesh, for small games). |
| `port` | 7777 | The UDP port a host listens on. `JM_NET_PORT` overrides it. |
| `maxPlayers` | 8 | Further players are turned away ("the session is full"). |
| `playerPrefab` | none | The host spawns one per player, at the scene's entities named `spawn` (cycled by player id), whenever a scene with spawn points loads and when a player joins. Scenes without spawn points (menus, lobbies) get none. |
| `sendRate` | 30 | How many times a second each process sends what it simulates. |
| `interpolationDelay` | 0.1 | How far behind (in seconds) copies show their entities, so there are two updates to move between. |
| `shareScene` | true | Whether joiners load the host's scene and follow its scene changes. A server that isn't a game world (a matchmaker) turns this off. |
| `server` | | Settings only for the dedicated server (below). Its `topology`, `port`, `maxPlayers` and `shareScene` override the ones above there. |

The editor edits all of this in **Project Settings > Multiplayer**.

## Who simulates what

Every shared entity has an **owner** and may have a **controller**:

- The **owner** simulates it. The owner is the host (`Net.HOST`, -1), or a
  player id (0, 1, ...).
- The **controller** is the player whose input its script reads, or -1 for
  none.

| How it came to be | Owner | Controller |
|---|---|---|
| In a scene file | the host | none |
| Spawned by a script | the spawning entity's owner (the host, for scripts on unshared entities) | the spawning entity's controller |
| `Net.spawnPlayer(p, ...)` and `playerPrefab`, with `authority: "host"` | the host | p |
| ... with `authority: "owner"` | p | p |

So a bullet fired by a player's avatar belongs to whoever simulates the
avatar, and its controller says who fired it.

Offline, every entity is simulated here, and the game runs exactly as a
single-player one would.

### The Network component

```json
"NetworkComponent": {
  "authority": "host",
  "replicate": ["TransformComponent", "SpriteComponent"],
  "interpolate": true,
  "scripts": "authority",
  "ownerLeaves": "destroy"
}
```

| Field | Default | Meaning |
|---|---|---|
| `authority` | `"host"` | For entities spawned for a player. `"host"`: the host simulates it and the player controls it with their input. This is server-authoritative: nobody can cheat by editing their own copy, and every action waits a round trip. `"owner"`: the player's own machine simulates it. There's no input lag, and the player is trusted. |
| `replicate` | `["TransformComponent"]` | The components whose script fields (x, y, rotation, scale, velocity, sprite color...) are streamed. |
| `interpolate` | true | Copies move smoothly between updates instead of jumping to each one. Rotation takes the short way round. |
| `scripts` | `"authority"` | `"authority"`: the script runs only where the entity is simulated. `"everywhere"`: it runs on every copy too, and `me.isMine` tells the script which one it is (use this for presentation: colors, animations, local effects). |
| `ownerLeaves` | `"destroy"` | When its owner or controller leaves. `"destroy"`: it goes, on every machine. `"host"`: the host takes it over. |

What else is shared, from whoever simulates the entity: its **tags**, its
**`entity.data`** store, and its **destruction**. The host may also destroy any
shared entity, which removes it everywhere. Spawning a shared prefab shares
the new entity. Spawning a prefab without a Network component stays local.

Writes to a copy (a field, its data, its tags) last only until its owner's
next update. To change something someone else simulates, send it a message.

## Scenes

The host's scene is everyone's. A joiner loads it, and when the host loads
another scene, or spawns or despawns a group, everyone does too. Shared
entities in a scene file pair up by their place in the file, so nothing about
them needs to be sent but their state. A client's own `Scene.load()` while in a
session changes only its own screen, which is useful for leaving: call
`Net.leave()`, then load the menu.

## The session store

`GameState` (the session store) is the host's, mirrored to everyone: scores,
the round's clock, the phase of the match. Clients read it. A client's own
writes stay local, until the host changes the same key. Keys that start with
`local.` are never mirrored, so they stay on their machine (the checkers demo
keeps the opponent's name in one).

`Save` (the save file) is always local.

**Keep a game's state in data, not in script variables.** If the host leaves a
p2p session, a new host's copy of each host-owned script starts fresh from its
top-level code. Whatever it needs to carry on must be in `GameState`,
`entity.data`, or component fields. Pellet Party's director keeps the round's
phase and clock in `GameState`, for this reason.

## Input

A script on an entity that a remote player controls reads that player's
input. `Input.down("fire")`, `Input.axis(...)`, `Input.pressed(...)`,
`Input.repeated(...)` and raw keys all answer for the controlling player,
sent from their machine whenever it changes (with their own bindings and
gamepads). Every other script reads this machine's devices. The pointer and
the mouse wheel aren't sent: send what they mean as a message.

So the same `tank.ts` that reads the arrow keys in single player drives every
tank on the server, each from its own player.

## Scripting: `Net`

```ts
import { Net, NetRole, NetStatus, NetTopology } from "@jm/runtime";
```

| | |
|---|---|
| `Net.host(port = 0, topology = NetTopology.None)` | Starts a session others can join. `port` 0 uses `net.port`. A p2p host started with port 0 keeps the socket this machine already has open (see matchmaking). Returns false if it can't; `Net.error` says why. |
| `Net.join("host:port")` | Joins a session. `Net.status` goes from `Connecting` to `Connected`, or to `Disconnected` (refused, unreachable, wrong game or version), with `Net.error` saying why. |
| `Net.leave()` | Leaves. Copies of entities others simulate are removed. Yours stay, now local. |
| `Net.role` | `Offline`, `Server` (a dedicated server), `Host` (hosting and playing as player 0), or `Client`. |
| `Net.online`, `Net.isHost`, `Net.isServer` | Whether you're in a session, whether you host it, and whether you're a dedicated server. |
| `Net.localPlayer`, `Net.hostPlayer` | Player ids. The dedicated server is -1. |
| `Net.players()`, `Net.joined()`, `Net.left()` | Everyone in the session; who joined or left since the last frame. On joining, `joined()` lists everyone already there. |
| `Net.playerName(p)`, `Net.setName(name)` | Names. Set yours before hosting or joining (`JM_NET_NAME` does it too). |
| `Net.playerAddress(p)` | "ip:port" as this machine sees them. A matchmaker hands it to their opponent. |
| `Net.ping(p)` | Round trip, in seconds. |
| `Net.spawnPlayer(p, prefab, x, y, overrides?)` | Host only: spawns an entity for player `p` (see the ownership table above). |
| `Net.send(p, name, text?, number?)` | A message to a player: `p`, `Net.HOST` or `Net.EVERYONE`. |
| `Net.messages()` | The messages that arrived since the last frame: each has `from`, `name`, `text` and `number`. Every script reads the same list. |
| `Net.punch("ip:port")` | Opens this machine's router toward an address for a few seconds (see matchmaking). |

On entities:

| | |
|---|---|
| `me.isMine` | Whether this process simulates it. Always true for unshared entities, and offline. |
| `me.isShared` | Whether it has a Network component. |
| `me.owner`, `me.controller` | As above. `Net.HOST` (-1) is the host. |
| `entity.send(name, text?, number?)` | For a shared entity, this goes to the machine that simulates it. Messages are how players ask the host for things ("eat this pellet", "move this piece"). |
| `entity.broadcast(name, text?, number?)` | Delivered to every copy, on every machine. |
| `message.player` (in `onMessage`) | The player whose machine sent it: `Net.HOST` for a dedicated server, -2 offline. |

## Dedicated servers

`journeyman_server` is the engine linked without its frontend modules: no
window, renderer, UI or audio. A module is in a build when its library is
linked, so the server is the same engine with fewer libraries
(`engine/CMakeLists.txt`). It runs the game's own files:

- It hosts as soon as it starts, on `net.server.port`, else `net.port` (or
  `JM_NET_PORT`), and keeps a steady `net.tickRate` frames a second (default 60).
- It starts in `net.server.entryScene`, if set, instead of the game's first scene.
- `net.server.scripts` are scripts only the server runs, each on an entity of
  its own outside every scene, for as long as it runs: match rules, bots, a
  matchmaker. This is where a game's server-side additions go.
- Scripts' calls into the parts it lacks do nothing and return 0 or empty:
  sounds, UI, sprites, the camera, post effects. Component fields it doesn't
  have read 0. So scripts don't need `if (!Net.isServer)` around presentation,
  but they can check.
- It stops cleanly on Ctrl-C or SIGTERM: its players are told, and logs and
  dumps are written.

```sh
jm run --server                 # the server, on this machine, from build/
jm export --server              # dist/<Name>-server: one executable with the game inside
jm pack --server                # just the archive: no images, sounds, UI, shaders or fonts
```

An exported server needs nothing installed. Run it where players can reach
its UDP port. Releases include `journeyman_server` beside `jm`, and
`journeyman-server-<platform>` for `jm export --server --target <platform>`.

## Matchmaking and NAT

A game can use a dedicated server only to bring players together, then let
them play peer to peer. That's the checkers demo:

1. The game's `net` is `p2p` (the match). Its `net.server` is a client/server
   matchmaker with `shareScene: false`, an empty entry scene, and a matchmaker
   script.
2. Players `Net.join()` the matchmaker. Its script pairs them and sends each
   one a message naming the other's `Net.playerAddress()`.
3. Both `Net.leave()`. One calls `Net.host(0, NetTopology.P2P)`, which keeps
   the same UDP socket, so the address the matchmaker saw still reaches it, and
   then `Net.punch(otherAddress)`. The other calls `Net.join(hostAddress)`.

Most home routers let this through. Strict (symmetric) NATs can still refuse,
and the game should handle the join failing. There's no relay. P2P also needs
every pair of players to reach each other: in a mesh, peers dial each other
using the addresses the host saw.

## Running sessions on one machine

```sh
jm run --peers 3                     # a whole session: a server (if the game has one) and 3 windows
jm run --peers 2 --latency 120 --loss 0.05
jm run --host / --join 127.0.0.1:7777
```

With `--peers`, each game gets its own save folder, and its own capture, dump,
trace and error paths (`frames/peer1/...`, `net.peer1.jsonl`). Output is
prefixed with `[peer1]` and `[server]`. `JM_INPUT_REPLAY=replay.{peer}.txt`
gives each peer its own replay. In the editor, **Play > Play with Players...**
does the same.

For tools and tests:

| Variable | |
|---|---|
| `JM_NET_HOST=1` (or a port), `JM_NET_JOIN=host:port` | Host or join as the game starts. |
| `JM_NET_NAME`, `JM_NET_PORT` | This player's name; the port to host on. |
| `JM_NET_LATENCY=ms`, `JM_NET_JITTER=ms`, `JM_NET_LOSS=0..1` | Simulated trouble on everything this process sends. Seeded. |
| `JM_NET_TRACE=net.jsonl` | One JSON line per message: time, frame, direction, type, bytes, peer. |
| `JM_REALTIME=1` | An automated (fixed-step) run keeps to the clock, so it can talk to other processes. |
| `JM_WINDOW_POS=x,y` | Where the window opens. |

For agents, `jm mcp`'s `session` tool plays a session headless (per-peer
replays, latency, loss) and returns each peer's final state.

State dumps (`JM_DUMP_DIR`) have a `net` section: role, player, players, and
every shared entity's id, owner and controller. `scripts/check-multiplayer.py`
plays a demo's session as described in its `tests/multiplayer.json` (peers,
frames, and what every peer must see), with no GPU, and fails unless every
peer agrees. CI runs it for every multiplayer demo.

## How it works

- **Transport:** ENet over UDP, with a reliable ordered channel (joining,
  spawns, destroys, scenes, data, tags, the session store, input, messages)
  and an unreliable one (field updates, newest wins). There's one socket per
  process, kept across sessions.
- **Joining:** the joiner sends Hello (the protocol version, the game's name and
  version, and its player name). The host answers Welcome (the joiner's id,
  the players, the session store), then the scene, its shared entries' ids, and
  every runtime-spawned shared entity with its current state, data and tags.
  P2P joiners then dial every other peer, and each newly linked pair exchanges
  what they simulate.
- **State:** at `sendRate`, each process sends the replicated fields of what it
  simulates. A change goes out in the next three sends, and every field goes
  out about once a second, so a lost update heals. Copies keep timestamped
  samples. Each frame after physics, they're placed at their owner's clock
  minus `interpolationDelay`, measured against that owner's clock.
- **Relaying:** in client/server, the host forwards what clients send about
  their own entities to the other clients. A process accepts changes to an
  entity only from its owner, or from the host.
- **Host migration (p2p):** when the host leaves, the lowest remaining player
  id hosts. Every peer works this out the same way, so no message is needed.
  Host-owned entities' scripts start on the new host.
- **Limits:** the whole state is sent to everyone, with no interest management;
  fine for small games. Fields are raw 32-bit values. There's no client-side
  prediction: use `authority: "owner"` where input lag matters, and accept
  trusting that client. The pointer isn't networked. An entity spawned at run
  time replicates as a whole prefab; its children with their own Network
  components don't (put the Network component on the root).
