// A slime that walks into things. Stomped: flattens. Hit by a shell: knocked out.
import { Timer, self } from "@jm/runtime";
import { Walker } from "./lib/walker";
import { Session } from "./lib/session";

const me = self();
const walker = new Walker(me, 7, 8, 28);
const squashed = new Timer();  // counts down the flattened body's last moments

export function onUpdate(dt: f32): void {
  if (squashed.tick(dt)) me.destroy();
  if (!squashed.ready) return;
  if (me.hasTag("stomped")) {
    me.removeTag("enemy");
    me.collider.layerMask = 0;
    me.sprite.play("flat");
    Session.addScore(100);
    squashed.start(0.5);
    return;
  }
  if (me.hasTag("hit") && !walker.knockedOut) {
    Session.addScore(100);
    walker.knockOut();
  }
  walker.update(dt);
}
