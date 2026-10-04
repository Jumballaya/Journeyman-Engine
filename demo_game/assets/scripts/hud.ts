// Mirrors the session into the HUD document every frame.
import { UI } from "@jm/runtime";
import { scoreText } from "./lib/util";
import { Session, liveHiscore } from "./lib/session";
import { setVisible } from "./lib/screens";

export function onUpdate(dt: f32): void {
  UI.setText("score", scoreText(Session.score));
  UI.setText("hiscore", scoreText(liveHiscore()));
  UI.setText("power", Session.power >= 3 ? "MAX" : "LV " + Session.power.toString());
  UI.showCount("life", Session.lives, 5, "hidden");
  UI.showCount("bomb", Session.bombs, 5, "hidden");
  setVisible("boss", Session.bossActive);
  if (Session.bossActive) UI.fill("boss-fill", <f32>Session.bossHealth);
}
