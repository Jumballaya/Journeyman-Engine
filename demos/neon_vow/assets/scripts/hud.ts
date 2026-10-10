import { GameState, Input, Scene, Time, UI, formatNumber } from "@jm/runtime";

function clock(seconds: f64): string {
  return formatNumber(Math.floor(seconds / 60), 2) + ":" + formatNumber(seconds % 60, 2);
}

export function onUpdate(dt: f32): void {
  const shards = formatNumber(GameState.getNumber("shards"), 2);
  const total = formatNumber(GameState.getNumber("shardTotal"), 2);
  const lives = <i32>GameState.getNumber("lives", 3);
  const clear = GameState.getBool("levelClear"), over = GameState.getBool("gameOver");
  UI.setText("shards", shards + " / " + total);
  UI.setText("lives", lives.toString());
  UI.showCount("life", lives, 3, "spent");
  UI.setText("clock", clock(GameState.getNumber("seconds")));
  UI.setVisible("pod-hint", GameState.getNumber("pod") > 0, "hidden");
  UI.setVisible("rail-hint", GameState.getNumber("checkpoint") == 3 && !GameState.getBool("cartArrived") && !over, "hidden");
  UI.setText("rail-hint", GameState.getNumber("cartPhase") == 0 ? "MAG-RAIL / JUMP ABOARD" : "HOLD JUMP / CLEAR ARCS & SENTRIES");
  UI.setVisible("result", clear || over, "hidden");
  UI.setText("result-title", clear ? "VOW FULFILLED" : "LIGHT EXTINGUISHED");
  UI.setText("result-label", clear ? "THE SILENT WARD  /  LEVEL CLEAR" : "THE SILENT WARD  /  GAME OVER");
  UI.setText("result-shards", shards + " / " + total + " LIGHT SHARDS");
  UI.setText("result-time", clock(GameState.getNumber("seconds")));
  if ((clear || over) && (Input.pressed("confirm") || Input.pressed("jump"))) {
    GameState.clear();
    Time.resume();
    Scene.load("main");
  }
}
