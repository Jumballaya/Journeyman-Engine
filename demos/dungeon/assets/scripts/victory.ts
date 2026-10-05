// The ending card: gems collected, a record if beaten; confirm returns to the title.
import { Input, Scene, UI } from "@jm/runtime";
import { Session } from "./lib/session";

let t: f32 = 0;
let leaving = false;
UI.setText("gems", Session.gems.toString());
UI.setVisible("record", Session.recordGems(), "hidden");

export function onUpdate(dt: f32): void {
  t += dt;
  if (leaving || t < 2 || !Input.pressed("confirm")) return;
  leaving = true;
  Scene.transition("title", 0.6);
}
