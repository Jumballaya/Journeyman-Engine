// The current run, shared by every script through GameState.
import { GameState, Save } from "@jm/runtime";
import { LEVELS } from "./levels";

export enum Outcome { Playing, Dead, Clear }

export class Session {
  static get started(): bool { return GameState.has("lives"); }

  static newGame(): void {
    GameState.clear();
    GameState.setNumber("lives", 3);
    GameState.setString("level", LEVELS[0].id);
  }

  static get level(): string { return GameState.getString("level", LEVELS[0].id); }
  static set level(id: string) { GameState.setString("level", id); }

  static get score(): f64 { return GameState.getNumber("score"); }
  static addScore(points: f64): void { GameState.add("score", points); }

  static get lives(): i32 { return <i32>GameState.getNumber("lives"); }
  static set lives(n: i32) { GameState.setNumber("lives", n); }

  static get coins(): i32 { return <i32>GameState.getNumber("coins"); }
  // Every 100th coin is an extra life; true when this coin was one.
  static addCoin(): bool {
    const coins = Session.coins + 1;
    GameState.setNumber("coins", coins % 100);
    Session.addScore(200);
    if (coins < 100) return false;
    Session.lives++;
    return true;
  }

  // Grown by a mushroom; kept between levels, lost on a hit or a death.
  static get big(): bool { return GameState.getBool("big"); }
  static set big(on: bool) { GameState.setBool("big", on); }

  // Seconds left on the level clock, published by the level for the HUD.
  static get time(): i32 { return <i32>GameState.getNumber("time"); }
  static set time(n: i32) { GameState.setNumber("time", n); }

  // How the level attempt ended, raised by Pip and handled by the level script.
  static get outcome(): Outcome { return <Outcome>GameState.getNumber("outcome"); }
  static set outcome(o: Outcome) { GameState.setNumber("outcome", o); }

  // Marks a brick broken during this attempt (see beginAttempt).
  static tileKey(tx: i32, ty: i32): string {
    return "tile." + GameState.getNumber("attempt").toString() + "." + tx.toString() + "." + ty.toString();
  }
  // A fresh attempt: no outcome yet, and every brick of the last one is back.
  static beginAttempt(): void {
    GameState.add("attempt", 1);
    Session.outcome = Outcome.Playing;
  }

  static recordHiscore(): bool { return Save.record("hiscore", Session.score); }
  static get hiscore(): f64 { return Math.max(Save.getNumber("hiscore"), Session.score); }
}
