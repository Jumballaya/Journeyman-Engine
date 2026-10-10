// Title: find an opponent through the matchmaker (assets/data/net.json). Its
// answer says who hosts the match; then this machine leaves the matchmaker
// and the two connect to each other directly.
import { App, Data, GameState, Input, Menu, Net, NetStatus, NetTopology, Scene, UI } from "@jm/runtime";
import { NOTE, OPPONENT, Pairing } from "./lib/match";

const address = Data.json("assets/data/net.json").get("matchmaker").text("127.0.0.1:7783");
const menu = new Menu(["find", "quit"]);
// Started with JM_NET_JOIN (jm run --peers): already on the way to the matchmaker.
let searching = Net.status == NetStatus.Connecting || Net.status == NetStatus.Connected;

UI.setText("status", GameState.getString(NOTE));
GameState.remove(NOTE);
menu.render();

function status(text: string): void { UI.setText("status", text); }

function matched(pairing: Pairing): void {
  GameState.setString(OPPONENT, pairing.opponent);
  // The socket stays open across sessions: the matchmaker saw this machine at
  // the address it gave our opponent.
  Net.leave();
  if (pairing.hosting) {
    if (!Net.host(0, NetTopology.P2P)) {
      GameState.setString(NOTE, Net.error.toUpperCase());
      return;
    }
    Net.punch(pairing.address);  // let them through our router
  } else if (!Net.join(pairing.address)) {
    GameState.setString(NOTE, Net.error.toUpperCase());
    return;
  }
  Scene.load("versus");
}

export function onUpdate(dt: f32): void {
  if (searching) {
    if (Net.status == NetStatus.Disconnected) {
      searching = false;
      status("NO MATCHMAKER AT " + address + ": " + Net.error.toUpperCase());
      return;
    }
    status(Net.status == NetStatus.Connecting ? "REACHING THE MATCHMAKER ..." : "LOOKING FOR AN OPPONENT ...  ESC CANCELS");
    const messages = Net.messages();
    for (let i = 0; i < messages.length; i++) {
      if (messages[i].name != "match") continue;
      const pairing = Pairing.parse(messages[i].text);
      if (pairing !== null) return matched(pairing);
    }
    if (Input.justPressed("back")) {
      Net.leave();
      searching = false;
      status("");
    }
    return;
  }
  const choice = menu.update();
  if (choice == "find") {
    if (Net.join(address)) searching = true;
    else status(Net.error.toUpperCase());
  } else if (choice == "quit") {
    App.quit();
  }
}
