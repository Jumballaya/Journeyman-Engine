// A save crystal: records the journey to the save file.
import { Sound, self } from "@jm/runtime";
import { Dialog } from "./lib/dialog";
import { Party } from "./lib/party";
import { mapById } from "./lib/maps";

const me = self();
const dialog = new Dialog();
let asking = false;

export function onUpdate(dt: f32): void {
  if (me.hasTag("talk") && !dialog.open) {
    me.removeTag("talk");
    asking = true;
    dialog.show(["THE CRYSTAL HUMS SOFTLY.", "RECORD YOUR JOURNEY?"], ["YES", "NO"]);
  }
  dialog.update(dt);
  if (!asking || dialog.open) return;
  asking = false;
  if (dialog.answer != 0) return;
  Party.placeAt(Party.map, me.transform.x, me.transform.y - 16);  // resume just below the crystal
  Party.save();
  new Sound("save").play(0.6);
  dialog.show(["YOUR JOURNEY WAS RECORDED IN " + mapById(Party.map).name + "."]);
}
