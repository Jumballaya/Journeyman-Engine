// Hit points: a "damage" message takes its number of them (1 without one),
// "heal" gives them back. At 0 it spawns `deathPrefab` and is destroyed. Hits
// within `invulnerable` seconds of the last don't count. With `stateKey`, the
// session (GameState) holds what's left, for a HUD. Touching something tagged
// `touchTag` sends it `touchDamage` damage.
import { Entity, GameState, Health, Message, Params, self, spawn } from "@jm/runtime";

const me = self();
const health = new Health(<f32>Params.number("hp", 3));
const invulnerable = <f32>Params.number("invulnerable", 0.5);
const deathPrefab = Params.text("deathPrefab", "");
const stateKey = Params.text("stateKey", "");
const touchTag = Params.text("touchTag", "");
const touchDamage = Params.number("touchDamage", 1);
let safe: f32 = 0;
publish();

function publish(): void {
  if (stateKey != "") GameState.setNumber(stateKey, health.current);
}

export function onUpdate(dt: f32): void { safe -= dt; }

export function onMessage(message: Message): void {
  const amount = <f32>(message.number > 0 ? message.number : 1);
  if (message.name == "heal" && !health.dead) {
    health.heal(amount);
    publish();
  }
  if (message.name != "damage" || health.dead || safe > 0) return;
  safe = invulnerable;
  const killed = health.damage(amount);
  publish();
  if (!killed) return;
  if (deathPrefab != "") spawn(deathPrefab, me.transform.x, me.transform.y);
  me.destroy();
}

export function onOverlapStart(other: Entity): void {
  if (touchTag != "" && other.hasTag(touchTag)) other.send("damage", "", touchDamage);
}
