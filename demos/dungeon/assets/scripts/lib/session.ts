// The adventure so far, shared by every script through GameState.
import { GameState, Save } from "@jm/runtime";

export enum Arrival { Start, Stairs }

export class Session {
  static get started(): bool { return GameState.has("maxHearts"); }

  static newGame(): void {
    GameState.clear();
    GameState.setNumber("maxHearts", 3);
    GameState.setNumber("health", 6);
    GameState.setString("area", "grove");
  }

  // Health is counted in half hearts.
  static get maxHearts(): i32 { return <i32>GameState.getNumber("maxHearts"); }
  static get health(): i32 { return <i32>GameState.getNumber("health"); }
  static set health(halves: i32) { GameState.setNumber("health", max(0, min(halves, Session.maxHearts * 2))); }
  static addHeartContainer(): void {
    GameState.add("maxHearts", 1);
    Session.health = Session.maxHearts * 2;
  }

  static get gems(): i32 { return <i32>GameState.getNumber("gems"); }
  static addGems(n: i32): void { GameState.setNumber("gems", min(Session.gems + n, 999)); }
  static get keys(): i32 { return <i32>GameState.getNumber("keys"); }
  static set keys(n: i32) { GameState.setNumber("keys", n); }

  static get hasSword(): bool { return GameState.getBool("sword"); }
  static set hasSword(on: bool) { GameState.setBool("sword", on); }

  static get area(): string { return GameState.getString("area", "grove"); }
  static set area(id: string) { GameState.setString("area", id); }
  static get arrival(): Arrival { return <Arrival>GameState.getNumber("arrival"); }
  static set arrival(a: Arrival) { GameState.setNumber("arrival", a); }

  // The room the hero is in ("x,y"), published by the hero for the area.
  static get room(): string { return GameState.getString("room"); }
  static set room(r: string) { GameState.setString("room", r); }

  // One-time world changes: doors opened, items taken, the boss beaten.
  static done(what: string): bool { return GameState.getBool("done." + what); }
  static markDone(what: string): void { GameState.setBool("done." + what, true); }

  static get won(): bool { return GameState.getBool("won"); }
  static set won(on: bool) { GameState.setBool("won", on); }

  static recordGems(): bool { return Save.record("mostGems", Session.gems); }
}

// Names a map position for Session.done(): "crypt.7.10".
export function placeKey(area: string, tx: i32, ty: i32): string {
  return area + "." + tx.toString() + "." + ty.toString();
}
