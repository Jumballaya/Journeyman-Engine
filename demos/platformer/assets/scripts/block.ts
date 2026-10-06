// A bumpable block: "?" (a coin), "M" (a mushroom) or "B" (a brick that big
// Pip smashes), param "kind". It makes its own map tile solid, so it can be
// placed anywhere. The player sends it "bump" or "smash".
import { Message, Overrides, Params, Sound, TileMap, self, spawn } from "@jm/runtime";
import { Session } from "./lib/session";
import { TILE, tileOf, tileTag } from "./lib/tiles";

const BOUNCE_SECONDS: f32 = 0.16;

const me = self();
const kind = Params.text("kind", "?");
const tx = tileOf(me.transform.x);
const ty = tileOf(me.transform.y);
const baseY = me.transform.y;
TileMap.find("map").set(tx, ty, kind);
me.addTag(tileTag(tx, ty));  // how Pip finds the block it bumps
let used = false;
let bounce: f32 = -1;  // seconds into the bump animation, or -1

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
  TileMap.find("map").set(tx, ty, ".");  // open for good: the map reloads with the next attempt
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
