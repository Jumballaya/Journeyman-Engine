// A beetle. Stomped, it hides in its shell; touching a still shell kicks it,
// and a moving shell knocks out every enemy it hits until it is stomped again.
import { Entity, Message, Sound, self } from "@jm/runtime";
import { Walker } from "./lib/walker";
import { Session } from "./lib/session";

enum State { Walking, Shell, Sliding }

const WALK_SPEED: f32 = 28;
const SLIDE_SPEED: f32 = 210;
const ENEMY: u32 = 2;
const SHELL: u32 = 8;

const me = self();
const walker = new Walker(me, 7, 8, WALK_SPEED);
const kick = new Sound("kick");
let state = State.Walking;

function hide(): void {
  state = State.Shell;
  walker.speed = 0;
  me.sprite.play("shell");
  me.addTag("shell_idle");
  me.collider.layerMask = ENEMY;
  me.collider.collidesWithMask = 0;
}

function slide(direction: f32): void {
  state = State.Sliding;
  walker.direction = direction;
  walker.speed = SLIDE_SPEED;
  me.removeTag("shell_idle");
  me.collider.layerMask = ENEMY | SHELL;  // still hurts Pip; now also hits enemies
  me.collider.collidesWithMask = ENEMY;
  kick.play(0.6);
}

// From Pip: "stomp", or "kick" (number: the direction); from a shell: "hit".
export function onMessage(message: Message): void {
  if (message.name == "hit" && !walker.knockedOut) {
    Session.addScore(200);
    walker.knockOut();
  } else if (message.name == "stomp") {
    if (state == State.Walking) Session.addScore(100);
    hide();
  } else if (message.name == "kick") {
    slide(<f32>message.number);
  }
}

export function onUpdate(dt: f32): void {
  walker.update(dt);
}

export function onCollide(other: Entity): void {
  if (state == State.Sliding && other.hasTag("enemy") && !other.hasTag("king")) {
    other.send("hit");
    kick.play(0.5);
  }
}
