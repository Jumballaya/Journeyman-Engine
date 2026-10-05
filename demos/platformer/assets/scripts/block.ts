// A bumpable block: "?" (a coin), "M" (a mushroom) or "B" (a brick that big
// Pip smashes). The player sends it "bump" or "smash".
import { GameState, Message, Overrides, Params, Sound, self, spawn } from "@jm/runtime";
import { Session } from "./lib/session";
import { TILE, tileTag } from "./lib/tiles";

const BOUNCE_SECONDS: f32 = 0.16;

const me = self();
const kind = Params.text("kind", "?");
const tx = <i32>Params.number("tx");
const ty = <i32>Params.number("ty");
const baseY = me.transform.y;
let used = false;
let bounce: f32 = -1;  // seconds into the bump animation, or -1

me.addTag(tileTag(tx, ty));

function release(): void {
  if (kind == "M") {
    spawn("mushroom", me.transform.x, baseY + TILE);
    new Sound("sprout").play(0.6);
  } else {
    spawn("coin_pop", me.transform.x, baseY + TILE);
    new Sound(Session.addCoin() ? "oneup" : "coin").play(0.6);
  }
  used = true;
  me.sprite.play("used");
}

function smash(): void {
  GameState.setBool(Session.tileKey(tx, ty), true);  // the tile map now treats it as open
  for (let i = 0; i < 4; i++) {
    const side: f32 = i % 2 == 0 ? -1 : 1;
    spawn("debris", me.transform.x + side * 4, baseY + (i < 2 ? 4 : -4),
          new Overrides().velocity(side * 60, i < 2 ? 320 : 220));
  }
  new Sound("break").play(0.7);
  me.destroy();
}

export function onMessage(message: Message): void {
  if (message.name == "smash" && kind == "B") {
    smash();
  } else if (!used) {
    bounce = 0;
    if (kind != "B") release();
  }
}

export function onUpdate(dt: f32): void {
  if (bounce >= 0) {
    bounce += dt;
    const k = Mathf.min(bounce / BOUNCE_SECONDS, 1);
    me.transform.y = baseY + Mathf.sin(k * Mathf.PI) * 5;
    if (k >= 1) bounce = -1;
  }
}
