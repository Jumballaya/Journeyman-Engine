# Pellet Party

Two to four players, peer to peer: eat the most pellets before the clock runs
out. One player hosts; the others join, and every player connects to every
other directly. If the host leaves, the next player takes over and the round
goes on.

```sh
cd assets/scripts && npm install && cd ../..
jm build
jm run --peers 3       # a whole party on this machine: three windows, the first hosting
```

Or by hand: `jm run`, pick HOST A PARTY; on other machines (or windows)
`jm run` and JOIN. The address to join is `assets/data/net.json`
(`127.0.0.1:7781` by default; `jm run --join host:port` overrides it).

Arrows or WASD move. Enter picks. Esc leaves.

## How it's shared

- `.jm.json` `net`: topology `p2p`, port 7781, up to 4 players, and
  `playerPrefab: "player"`: the host spawns a blob for each player at the
  arena's `spawn` entities.
- **Blobs** (`player.prefab.json`) have `Network` authority `owner`: each is
  simulated on its own player's machine, so moving feels instant, and its
  Transform and Sprite (the color) stream to everyone else.
- **Pellets** and the **director** have `Network` with the default authority:
  the host simulates them. The director spawns pellets and runs the clock.
- **Eating:** a blob's script sees the collision on its own machine and sends
  the pellet `"eat"`; `entity.send` delivers it where the pellet is simulated
  (the host), whose script credits `message.player` once and destroys it.
- **The round** (phase, time left, scores) lives in `GameState`, which the
  host mirrors to everyone: the HUD reads it on every machine, and a new host
  carries on from it.
- **Scenes** follow the host: it loads the arena or the lobby, and everyone
  else does too.
