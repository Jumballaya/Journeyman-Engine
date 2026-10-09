// Until both players are connected to each other: the host waits for its
// opponent, then starts the game for both.
import { GameState, Net, NetStatus, Scene, Timer, UI } from "@jm/runtime";
import { NOTE, OPPONENT } from "./lib/match";

const GIVE_UP: f32 = 20;
const waited = new Timer();
const starting = new Timer();
let countingDown = false;
waited.start(GIVE_UP);

UI.setText("opponent", GameState.getString(OPPONENT).toUpperCase());
UI.setText("you", Net.isHost ? "YOU PLAY RED" : "YOU PLAY BLACK");

function giveUp(why: string): void {
  Net.leave();
  GameState.setString(NOTE, why);
  Scene.load("title");
}

export function onUpdate(dt: f32): void {
  waited.tick(dt);
  if (Net.status == NetStatus.Disconnected) return giveUp("LOST THE OPPONENT: " + Net.error.toUpperCase());
  const together = Net.players().length >= 2;
  UI.setText("status", together ? "CONNECTED" : Net.isHost ? "WAITING FOR THEM TO CONNECT ..." : "CONNECTING ...");
  if (!together && waited.ready) return giveUp("THE OPPONENT NEVER CONNECTED");
  if (Net.isHost && together) {
    if (!countingDown) {
      countingDown = true;
      starting.start(0.8);  // a moment to read who's who
    }
    if (starting.tick(dt)) Scene.load("board");
  }
}
