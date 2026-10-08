// Multiplayer: sessions, players, and messages between machines. Entities
// with a Network component are shared: one machine simulates each (runs its
// script) and the others show copies. See docs/networking.md.
import {
  __jmNetRole, __jmNetTopology, __jmNetStatus, __jmNetError, __jmNetLocalPlayer, __jmNetHostPlayer, __jmNetPort,
  __jmNetHost, __jmNetJoin, __jmNetLeave, __jmNetSetName, __jmNetPunch, __jmNetPlayers, __jmNetJoined, __jmNetLeft,
  __jmNetPlayerName, __jmNetPlayerAddress, __jmNetPing, __jmNetSpawnPlayer, __jmNetSendPlayer, __jmNetInboxCount,
  __jmNetInboxFrom, __jmNetInboxName, __jmNetInboxText, __jmNetInboxNumber,
} from "./env";
import { Entity } from "./entity";
import { Overrides } from "./world";
import { utf8, buf, cap, grow, text } from "./util";

export enum NetRole {
  Offline = 0,
  Server = 1,  // a dedicated server (journeyman_server): hosts, has no player of its own
  Host = 2,    // hosts and plays (player 0)
  Client = 3,  // joined someone's session
}

export enum NetTopology {
  None = 0,
  ClientServer = 1,  // everyone talks through the host
  P2P = 2,           // players also connect to each other directly
}

export enum NetStatus {
  Offline = 0,
  Connecting = 1,
  Connected = 2,
  Disconnected = 3,  // lost, refused or the host left: Net.error says why
}

// A message from another machine (Net.send).
export class NetMessage {
  constructor(readonly from: i32, readonly name: string, readonly text: string, readonly number: f64) {}
}

function ids(read: (out: usize, bytes: i32) => i32): i32[] {
  let out = new Int32Array(16);
  let n = read(out.dataStart, out.byteLength);
  if (n > out.length) {
    out = new Int32Array(n);
    n = min(read(out.dataStart, out.byteLength), n);
  }
  const list = new Array<i32>(n);
  for (let i = 0; i < n; i++) list[i] = out[i];
  return list;
}

function hostText(read: (out: usize, cap: i32) => i32): string {
  let n = read(buf(), cap());
  if (grow(n)) n = read(buf(), cap());
  return text(n, "");
}

export class Net {
  // Message targets (and Net.HOST a sender): the host, or every other player.
  static readonly HOST: i32 = -1;
  static readonly EVERYONE: i32 = -2;

  static get role(): NetRole { return <NetRole>__jmNetRole(); }
  static get topology(): NetTopology { return <NetTopology>__jmNetTopology(); }
  static get status(): NetStatus { return <NetStatus>__jmNetStatus(); }
  // Why the last session ended or couldn't start ("" if it didn't fail).
  static get error(): string { return hostText((o, c) => __jmNetError(o, c)); }
  static get online(): bool { return __jmNetRole() != NetRole.Offline; }
  // This machine hosts the session (a dedicated server, or a player hosting).
  static get isHost(): bool { const r = __jmNetRole(); return r == NetRole.Server || r == NetRole.Host; }
  // This machine is a dedicated server (no window, no player of its own).
  static get isServer(): bool { return __jmNetRole() == NetRole.Server; }
  // This machine's player id (0 hosts a game it plays), or -1 with no player.
  static get localPlayer(): i32 { return __jmNetLocalPlayer(); }
  static get hostPlayer(): i32 { return __jmNetHostPlayer(); }
  // The UDP port this machine uses (0 before any session).
  static get port(): i32 { return __jmNetPort(); }

  // Starts a session others can join, on `port` (0: .jm.json's net.port; a
  // p2p host keeps the port it already has, e.g. the one a matchmaker saw).
  // False if it can't (Net.error).
  static host(port: i32 = 0, topology: NetTopology = NetTopology.None): bool { return __jmNetHost(port, topology); }
  // Joins the session at "host:port". Net.status says how it goes.
  static join(address: string): bool {
    const a = utf8(address);
    return __jmNetJoin(a.dataStart, a.length);
  }
  // Ends this machine's part in the session (copies of others' entities go).
  static leave(): void { __jmNetLeave(); }
  // The name other players see; set it before joining or hosting.
  static setName(name: string): void {
    const n = utf8(name);
    __jmNetSetName(n.dataStart, n.length);
  }
  // Opens this machine's router toward `address` for a few seconds, so a
  // peer there can connect (p2p through NAT; a matchmaker gives the address).
  static punch(address: string): void {
    const a = utf8(address);
    __jmNetPunch(a.dataStart, a.length);
  }

  // Player ids in the session (this machine's included).
  static players(): i32[] { return ids((o, b) => __jmNetPlayers(o, b)); }
  // Players who joined, or left, since the last frame (on joining, everyone already there).
  static joined(): i32[] { return ids((o, b) => __jmNetJoined(o, b)); }
  static left(): i32[] { return ids((o, b) => __jmNetLeft(o, b)); }
  static playerName(player: i32): string { return hostText((o, c) => __jmNetPlayerName(player, o, c)); }
  // "ip:port" as the host sees them: what a matchmaker hands to their peer.
  static playerAddress(player: i32): string { return hostText((o, c) => __jmNetPlayerAddress(player, o, c)); }
  // Round trip to them, in seconds.
  static ping(player: i32): f32 { return __jmNetPing(player); }

  // Host only: spawns `prefab` for `player`, who controls it (their input
  // drives its script), or simulates it if its Network says authority "owner".
  static spawnPlayer(player: i32, prefab: string, x: f32, y: f32, overrides: Overrides | null = null): Entity {
    const p = utf8(prefab);
    const o = utf8(overrides === null ? "" : overrides.toJson());
    return Entity.unpack(__jmNetSpawnPlayer(player, p.dataStart, p.length, x, y, o.dataStart, o.length));
  }

  // A message to a player (Net.HOST, Net.EVERYONE), read with Net.messages()
  // there next frame. For a message to an entity, entity.send() reaches it
  // wherever it's simulated.
  static send(player: i32, name: string, text: string = "", number: f64 = 0): void {
    const n = utf8(name);
    const t = utf8(text);
    __jmNetSendPlayer(player, n.dataStart, n.length, t.dataStart, t.length, number);
  }
  // Messages that arrived for this machine since the last frame.
  static messages(): NetMessage[] {
    const count = __jmNetInboxCount();
    const list = new Array<NetMessage>(count);
    for (let i = 0; i < count; i++) {
      list[i] = new NetMessage(__jmNetInboxFrom(i), hostText((o, c) => __jmNetInboxName(i, o, c)),
                               hostText((o, c) => __jmNetInboxText(i, o, c)), __jmNetInboxNumber(i));
    }
    return list;
  }
}
