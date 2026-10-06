// A slime that walks into things. Stomped: flattens. Hit by a shell: knocked out.
import { Message, Timer, self } from "@jm/runtime";
import { Walker } from "./lib/walker";
import { Session } from "./lib/session";

const me = self();
const walker = new Walker(me, 7, 8, 28);
const squashed = new Timer();  // counts down the flattened body's last moments

// From Pip: "stomp"; from a shell: "hit".
export function onMessage(message: Message): void {
  if (!squashed.ready || walker.knockedOut) return;
  if (message.name == "stomp") {
    me.removeTag("enemy");
    me.collider.layerMask = 0;
    me.sprite.play("flat");
    Session.addScore(100);
    squashed.start(0.5);
  } else if (message.name == "hit") {
    Session.addScore(100);
    walker.knockOut();
  }
}

export function onUpdate(dt: f32): void {
  if (squashed.tick(dt)) me.destroy();
  if (!squashed.ready) return;
  walker.update(dt);
}
