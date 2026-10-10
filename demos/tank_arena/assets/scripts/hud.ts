// Scores, your armor and what just happened, from the session store and your
// tank's data, both mirrored from the server.
import { GameState, Input, Net, NetStatus, Scene, Timer, UI, World } from "@jm/runtime";
import { FEED, FEED_COUNT, KILLS_TO_WIN, PHASE, WINNER, css, scoreKey, tankName } from "./lib/arena";

const ROWS = 8;
const feedShown = new Timer();
let feedSeen: f64 = 0;

export function onUpdate(dt: f32): void {
  if (Net.isServer) return;  // the server has no screen
  if (Net.status == NetStatus.Disconnected || Input.justPressed("back")) {
    Net.leave();
    Scene.load("title");
    return;
  }

  const players = Net.players();
  if (!World.find("bot").isNone) players.push(-1);  // the server's bot, when it's playing
  for (let i = 0; i < ROWS; i++) {
    const id = "row" + i.toString();
    if (i >= players.length) {
      UI.setText(id, "");
      continue;
    }
    const p = players[i];
    const you = p == Net.localPlayer ? " <" : "";
    UI.setText(id, tankName(p) + " " + (<i32>GameState.getNumber(scoreKey(p))).toString() + you);
    UI.setStyle(id, "color", css(p));
  }

  // Your tank: the one you control.
  let armor = 0;
  const tanks = World.findAll("tank");
  for (let i = 0; i < tanks.length; i++) {
    if (tanks[i].controller == Net.localPlayer) armor = <i32>tanks[i].data.getNumber("hp");
  }
  UI.setText("armor", "ARMOR " + "#".repeat(max(0, armor)) + "-".repeat(max(0, 3 - armor)));

  const count = GameState.getNumber(FEED_COUNT);
  if (count != feedSeen) {
    feedSeen = count;
    UI.setText("feed", GameState.getString(FEED));
    feedShown.start(2.5);
  }
  feedShown.tick(dt);
  UI.setVisible("feed", !feedShown.ready, "hidden");

  const over = GameState.getString(PHASE) == "over";
  UI.setVisible("over", over, "hidden");
  if (over) {
    const winner = <i32>GameState.getNumber(WINNER, -1);
    UI.setText("winner", tankName(winner) + " WINS");
    UI.setStyle("winner", "color", css(winner));
  }
  UI.setText("goal", "FIRST TO " + KILLS_TO_WIN.toString());
}
