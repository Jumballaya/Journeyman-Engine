// Mirrors the session into the HUD document every frame.
import { UI } from "@jm/runtime";
import { pad, percent } from "./lib/util";
import { Session, liveHiscore } from "./lib/session";
import { setVisible } from "./lib/screens";

export function onUpdate(dt: f32): void {
  UI.setText("score", pad(Session.score));
  UI.setText("hiscore", pad(liveHiscore()));
  UI.setText("power", Session.power >= 3 ? "MAX" : "LV " + Session.power.toString());
  for (let i = 1; i <= 5; i++) {
    setVisible("life" + i.toString(), i <= Session.lives);
    setVisible("bomb" + i.toString(), i <= Session.bombs);
  }
  setVisible("boss", Session.bossActive);
  if (Session.bossActive) UI.setStyle("boss-fill", "width", percent(Session.bossHealth));
}
