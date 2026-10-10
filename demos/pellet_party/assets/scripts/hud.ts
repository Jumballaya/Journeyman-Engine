// Time left and the scores, on every machine, from the session store.
import { GameState, Input, Net, NetStatus, Scene, UI } from "@jm/runtime";
import { PHASE, TIME_LEFT, WINNER, colorName, css, scoreKey } from "./lib/players";

const SLOTS = 4;

export function onUpdate(dt: f32): void {
  if (Net.status == NetStatus.Disconnected || Input.justPressed("back")) {
    Net.leave();
    Scene.load("title");
    return;
  }
  const seconds = <i32>Math.ceil(GameState.getNumber(TIME_LEFT));
  UI.setText("time", seconds.toString());

  const players = Net.players();
  for (let i = 0; i < SLOTS; i++) {
    const id = "score" + i.toString();
    if (i >= players.length) {
      UI.setText(id, "");
      continue;
    }
    const p = players[i];
    UI.setText(id, colorName(p) + " " + (<i32>GameState.getNumber(scoreKey(p))).toString());
    UI.setStyle(id, "color", css(p));
  }

  const results = GameState.getString(PHASE) == "results";
  UI.setVisible("results", results, "hidden");
  if (results) {
    const winner = <i32>GameState.getNumber(WINNER, -1);
    UI.setText("winner", winner < 0 ? "NOBODY WINS" : colorName(winner) + " WINS");
    UI.setStyle("winner", "color", winner < 0 ? "#ffffff" : css(winner));
  }
}
