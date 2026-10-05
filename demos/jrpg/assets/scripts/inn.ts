// The innkeeper: a night's rest restores the whole party for a fee.
import { Params, Sound, self } from "@jm/runtime";
import { Dialog } from "./lib/dialog";
import { Party } from "./lib/party";

const me = self();
const price = <i32>Params.number("price", 10);
const dialog = new Dialog();
let asking = false;

export function onUpdate(dt: f32): void {
  if (me.hasTag("talk") && !dialog.open) {
    me.removeTag("talk");
    asking = true;
    dialog.show(["WELCOME TO THE SLEEPING FOX!", "A ROOM IS " + price.toString() + " GOLD. STAY THE NIGHT?"], ["YES", "NO"]);
  }
  dialog.update(dt);
  if (!asking || dialog.open) return;
  asking = false;
  if (dialog.answer != 0) return;
  if (Party.gold < price) {
    dialog.show(["YOU'RE SHORT ON GOLD, FRIEND."]);
    return;
  }
  Party.gold -= price;
  Party.healAll();
  new Sound("inn").play(0.6);
  dialog.show(["...", "GOOD MORNING! YOU LOOK WELL RESTED."]);
}
