// The mouth of the wyrm's lair: a warning, then the final battle.
import { GameState, Message, Scene, Sound, self } from "@jm/runtime";
import { Dialog } from "./lib/dialog";
import { Party } from "./lib/party";

const me = self();
const dialog = new Dialog();
let asking = false;

// The hero says "talk" when facing it.
export function onMessage(message: Message): void {
  if (message.name != "talk" || dialog.open) return;
  asking = true;
  dialog.show(["HEAT POURS FROM THE CAVE. SOMETHING VAST IS BREATHING.", "ENTER THE LAIR?"], ["YES", "NO"]);
}

export function onUpdate(dt: f32): void {
  dialog.update(dt);
  if (!asking || dialog.open) return;
  asking = false;
  if (dialog.answer != 0) return;
  Party.placeAt(Party.map, me.transform.x, me.transform.y - 16);
  GameState.setNumber("encounter", -1);  // the boss
  new Sound("encounter").play(0.7);
  Scene.transition("battle", 1.4, "swirl");
}
