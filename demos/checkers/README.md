# Checkers

Online checkers with a matchmaker: a small dedicated server pairs players
who are looking for a game, then the two leave it and play each other
directly, peer to peer. The server never sees the game.

```sh
cd assets/scripts && npm install && cd ../..
jm build
jm run --peers 2       # the matchmaker and two games, which find each other
```

By hand: `jm run --server` starts the matchmaker (UDP 7783); then `jm run`
in two other terminals and FIND A MATCH in each. The matchmaker's address is
`assets/data/net.json`. `jm export --server` makes it one executable to run
anywhere players can reach; the games then need its public address.

Arrows pick a square and Enter moves (or click). Red moves first. Esc leaves.
Rules: American checkers; captures are compulsory and chain; kings move both
ways. `jm test` runs the rule tests (`tests/rules.spec.ts`).

## How it fits together

- **One game, two kinds of session.** `.jm.json` `net` is `p2p` with two
  players: the match. `net.server` describes the matchmaker the same files
  make with `journeyman_server`: client/server on port 7783 for 64 players,
  `shareScene: false` (players keep their own screens while they wait), an
  empty `entryScene`, and `scripts: ["assets/scripts/server/matchmaker.ts"]`.
- **The matchmaker** (`server/matchmaker.ts`) queues players as they join and
  sends each pair a `"match"` message (`Net.send`): one hosts, the other joins,
  each with the other's address as the server saw it.
- **The hand-off** (`title.ts`): both `Net.leave()` the matchmaker. The host
  calls `Net.host(0, NetTopology.P2P)`, which keeps the UDP socket the
  matchmaker saw, and `Net.punch(theirAddress)` to open its router; the other
  `Net.join`s that address. Through most home routers that's enough to connect
  directly (strict NATs can still refuse; then they're told the opponent
  never connected).
- **The board** is one shared entity (`board.scene.json`): the game is its
  `entity.data`, which the host keeps. Its script runs on both machines
  (`scripts: "everywhere"`): each draws from its own side and sends its moves
  with `board.send("move", ...)`, which reaches the host's copy; only that
  copy (`me.isMine`) checks them against `lib/rules.ts` and plays them.
- `local.` keys in `GameState` (the opponent's name) stay on their machine;
  the host mirrors every other key to its peer.
