// Who's here. The host starts the round; everyone else follows its scene.
import { GameState, Input, Net, NetStatus, Scene, UI } from "@jm/runtime";
import { PHASE, css, colorName } from "./lib/players";

const SLOTS = 4;

UI.setText("role", Net.isHost ? "HOSTING ON UDP PORT " + Net.port.toString() : "IN THE PARTY");
UI.setText("hint", Net.isHost ? "ENTER STARTS   ESC CLOSES THE PARTY" : "WAITING FOR THE HOST   ESC LEAVES");

function render(): void {
  const players = Net.players();
  for (let i = 0; i < SLOTS; i++) {
    const row = "slot" + i.toString();
    if (i < players.length) {
      const p = players[i];
      const you = p == Net.localPlayer ? "  (YOU)" : "";
      const host = p == Net.hostPlayer ? "  HOST" : "";
      UI.setText(row, colorName(p) + "  " + Net.playerName(p).toUpperCase() + you + host);
      UI.setStyle(row, "color", css(p));
    } else {
      UI.setText(row, "...");
      UI.setStyle(row, "color", "#4a5274");
    }
  }
}
render();

export function onUpdate(dt: f32): void {
  if (Net.joined().length > 0 || Net.left().length > 0) render();
  if (Net.status == NetStatus.Disconnected || !Net.online || Input.pressed("back")) {
    Net.leave();
    Scene.load("title");
    return;
  }
  if (Net.isHost && Input.pressed("confirm")) {
    GameState.setString(PHASE, "start");
    Scene.load("arena");
  }
}
