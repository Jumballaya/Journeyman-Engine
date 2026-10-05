// The hermit by the fires. Talking opens a dialog;
// the first conversation ends with the sword.
import { Message, Sound } from "@jm/runtime";
import { Dialog } from "./lib/dialog";
import { Session } from "./lib/session";

const dialog = new Dialog();
let giving = false;  // the sword is handed over when this dialog closes

// The hero says "talk" when facing it.
export function onMessage(message: Message): void {
  if (message.name != "talk" || dialog.open) return;
  giving = !Session.hasSword;
  dialog.show(giving
    ? ["THE GROVE HAS GONE QUIET, CHILD.", "OGLOTH STIRS IN THE CRYPT BELOW THE SHRINE.", "TAKE THIS BLADE. STRIKE WITH Z."]
    : ["THE SHRINE LIES NORTH-EAST.", "TWO KEYS GUARD ITS HEART. BE BRAVE."]);
}

export function onUpdate(dt: f32): void {
  dialog.update(dt);
  if (giving && !dialog.open) {
    giving = false;
    Session.hasSword = true;
    new Sound("item").play(0.7);
  }
}
