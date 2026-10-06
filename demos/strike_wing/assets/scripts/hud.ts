// Mirrors the session into the HUD document every frame.
import { screen } from "./lib/presentation";
import { UI } from "@jm/runtime";
import { scoreText } from "./lib/util";
import * as Session from "./lib/session";
import { liveHiscore } from "./lib/session";

export function onUpdate(dt: f32): void {
  UI.setText("score", scoreText(Session.score.value));
  UI.setText("hiscore", scoreText(liveHiscore()));
  UI.setText("power", Session.power.value >= 3 ? "MAX" : "LV " + Session.power.value.toString());
  UI.showCount("life", Session.lives.value, 5, "hidden");
  UI.showCount("bomb", Session.bombs.value, 5, "hidden");
  screen.setVisible("boss", Session.bossActive.value);
  if (Session.bossActive.value) UI.fill("boss-fill", <f32>Session.bossHealth.value);
}
