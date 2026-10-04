// Mirrors session state into the HUD document every frame.
import { GameState, UI } from "@jm/runtime";
import { pad, displayHiscore, setVisible } from "./lib/game";

export function onUpdate(dt: f32): void {
  UI.setText("score", pad(GameState.getNumber("score")));
  UI.setText("hiscore", pad(displayHiscore()));
  const power = <i32>GameState.getNumber("power", 1);
  UI.setText("power", power >= 3 ? "MAX" : "LV " + power.toString());

  const lives = <i32>GameState.getNumber("lives");
  const bombs = <i32>GameState.getNumber("bombs");
  for (let i = 1; i <= 5; i++) {
    setVisible("life" + i.toString(), i <= lives);
    setVisible("bomb" + i.toString(), i <= bombs);
  }

  const bossActive = GameState.getNumber("bossActive") > 0;
  setVisible("boss", bossActive);
  if (bossActive) {
    const max = GameState.getNumber("bossHpMax", 1);
    const pct = Math.max(0, Math.min(100, GameState.getNumber("bossHp") / max * 100));
    UI.setStyle("boss-fill", "width", (<i32>pct).toString() + "%");
  }
}
