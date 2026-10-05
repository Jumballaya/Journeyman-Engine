// Someone to talk to. Param "lines": what they say ("|" between lines);
// "again": what they say on later visits (defaults to "lines").
import { Message, Params } from "@jm/runtime";
import { Dialog } from "./lib/dialog";

const dialog = new Dialog();
const first = Params.text("lines").split("|");
const again = Params.text("again").length > 0 ? Params.text("again").split("|") : first;
let talked = false;

// The hero says "talk" when facing it.
export function onMessage(message: Message): void {
  if (message.name != "talk" || dialog.open) return;
  dialog.show(talked ? again : first);
  talked = true;
}

export function onUpdate(dt: f32): void {
  dialog.update(dt);
}
