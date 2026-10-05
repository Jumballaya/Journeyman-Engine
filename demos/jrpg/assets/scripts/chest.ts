// A treasure chest (params "item", "count", "flag"): opened once, for good.
import { Params, Sound, self } from "@jm/runtime";
import { Dialog } from "./lib/dialog";
import { Party } from "./lib/party";

const me = self();
const flag = Params.text("flag");
const item = Params.text("item", "POTION");
const count = <i32>Params.number("count", 1);
const dialog = new Dialog();

if (Party.done(flag)) me.sprite.play("open");

export function onUpdate(dt: f32): void {
  if (me.hasTag("talk") && !dialog.open) {
    me.removeTag("talk");
    if (Party.done(flag)) {
      dialog.show(["IT'S EMPTY."]);
    } else {
      Party.markDone(flag);
      Party.inventory.add(item, count);
      me.sprite.play("open");
      new Sound("chest").play(0.6);
      const article = "AEIOU".includes(item.charAt(0)) ? "AN " : "A ";
      dialog.show(["FOUND " + (count > 1 ? count.toString() + " " : article) + item + (count > 1 ? "S!" : "!")]);
    }
  }
  dialog.update(dt);
}
