// Someone to talk to. Param "lines": what they say ("|" between lines);
// "again": what they say on later visits (defaults to "lines").
import { Params, self } from "@jm/runtime";
import { Dialog } from "./lib/dialog";

const me = self();
const dialog = new Dialog();
const first = Params.text("lines").split("|");
const again = Params.text("again").length > 0 ? Params.text("again").split("|") : first;
let talked = false;

export function onUpdate(dt: f32): void {
  if (me.hasTag("talk") && !dialog.open) {
    me.removeTag("talk");
    dialog.show(talked ? again : first);
    talked = true;
  }
  dialog.update(dt);
}
