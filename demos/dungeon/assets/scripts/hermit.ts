// The hermit by the fires. Talking (the hero tags him "talk") opens a dialog;
// the first conversation ends with the sword.
import { Sound, self } from "@jm/runtime";
import { Dialog } from "./lib/dialog";
import { Session } from "./lib/session";

const me = self();
const dialog = new Dialog();
let giving = false;  // the sword is handed over when this dialog closes

export function onUpdate(dt: f32): void {
  if (me.hasTag("talk") && !dialog.open) {
    me.removeTag("talk");
    giving = !Session.hasSword;
    dialog.show(giving
      ? ["THE GROVE HAS GONE QUIET, CHILD.", "OGLOTH STIRS IN THE CRYPT BELOW THE SHRINE.", "TAKE THIS BLADE. STRIKE WITH Z."]
      : ["THE SHRINE LIES NORTH-EAST.", "TWO KEYS GUARD ITS HEART. BE BRAVE."]);
  }
  dialog.update(dt);
  if (giving && !dialog.open) {
    giving = false;
    Session.hasSword = true;
    new Sound("item").play(0.7);
  }
}
