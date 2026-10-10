// Title: host a party, or join one at the address in assets/data/net.json.
import { App, Data, Input, Menu, Net, NetStatus, Scene, UI } from "@jm/runtime";

const address = Data.json("assets/data/net.json").get("join").text("127.0.0.1:7781");
const menu = new Menu(["host", "join", "quit"]);
// Started with JM_NET_JOIN (jm run --peers): already on the way.
let joining = Net.status == NetStatus.Connecting;

UI.setText("address", address);
UI.setText("status", Net.error.toUpperCase());  // why the last party ended, if it did
menu.render();

// Started with JM_NET_HOST: the party is open; on to the lobby.
if (Net.isHost) Scene.load("lobby");

export function onUpdate(dt: f32): void {
  if (joining) {
    if (Net.status == NetStatus.Disconnected) {
      joining = false;
      UI.setText("status", Net.error.toUpperCase());
    } else {
      // Connected: the host's scene arrives and replaces this one.
      UI.setText("status", "JOINING " + address + " ...  ESC CANCELS");
    }
    if (Input.justPressed("back")) {
      Net.leave();
      joining = false;
      UI.setText("status", "");
    }
    return;
  }
  const choice = menu.update();
  if (choice == "host") {
    if (Net.host()) Scene.load("lobby");
    else UI.setText("status", Net.error.toUpperCase());
  } else if (choice == "join") {
    if (Net.join(address)) joining = true;
    else UI.setText("status", Net.error.toUpperCase());
  } else if (choice == "quit") {
    App.quit();
  }
}
