// Title: join the server at the address in assets/data/net.json.
import { App, Data, Input, Menu, Net, NetStatus, UI } from "@jm/runtime";

const address = Data.json("assets/data/net.json").get("server").text("127.0.0.1:7782");
const menu = new Menu(["join", "quit"]);
// Started with JM_NET_JOIN (jm run --peers): already on the way.
let joining = Net.status == NetStatus.Connecting;

UI.setText("address", address);
UI.setText("status", Net.error.toUpperCase());  // why the last game ended, if it did
menu.render();

export function onUpdate(dt: f32): void {
  if (joining) {
    // Connected: the server's scene (the arena) arrives and replaces this one.
    if (Net.status == NetStatus.Disconnected) {
      joining = false;
      UI.setText("status", Net.error.toUpperCase());
    } else {
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
  if (choice == "join") {
    if (Net.join(address)) joining = true;
    else UI.setText("status", Net.error.toUpperCase());
  } else if (choice == "quit") {
    App.quit();
  }
}
