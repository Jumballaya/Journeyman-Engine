# Tank Arena

Up to eight tanks on a dedicated server. The server simulates everything:
every tank drives on its player's input, sent from their machine, so nobody's
copy of the game decides a hit but the server's.

```sh
cd assets/scripts && npm install && cd ../..
jm build
jm run --peers 2       # a server and two games on this machine
```

Or by hand: `jm run --server` in one terminal, then `jm run` and JOIN in as
many others as you like (the address is `assets/data/net.json`, or
`jm run --join host:port`). For a real server, `jm export --server` writes
`dist/Tank Arena-server`: one executable with the game inside, no window,
listening on UDP 7782 (`JM_NET_PORT` changes it).

Left and right turn, up and down drive, Space fires. Esc leaves.

## The server is the game

`journeyman_server` is the engine without its window, renderer, UI and audio,
running these same files. What's only the server's comes from `.jm.json`:

```json
"net": {
  "topology": "server", "port": 7782, "maxPlayers": 8, "playerPrefab": "tank",
  "server": {
    "entryScene": "scenes/arena.scene.json",
    "scripts": ["assets/scripts/server/rules.ts"]
  }
}
```

- `server.entryScene`: the server starts in the arena; games start at the title
  and follow the server there when they join.
- `server.scripts`: `rules.ts` runs only on the server, outside every scene:
  first to 10 wins, then a new match; a bot joins a player who's alone.

## How it's shared

- **Tanks** (`tank.prefab.json`): `Network` authority `host` with
  `playerPrefab`, so each is the server's, controlled by its player: in
  `tank.ts`, `Input.axis("left", "right")` reads that player's keys. With
  `scripts: "everywhere"` its script also runs on the clients' copies, where
  `me.isMine` is false: there it only colors the tank and dims it when wrecked.
- **Armor** is the tank's `entity.data` (`hp`, `wrecked`), mirrored to every
  copy, so the HUD shows your armor from your tank's copy.
- **Shells** are spawned by tank scripts on the server; their controller is the
  shooter. A shell that hits a tank sends it `"hit"` with the shooter's id.
- **Scores and the kill feed** are `GameState`, mirrored to everyone.
