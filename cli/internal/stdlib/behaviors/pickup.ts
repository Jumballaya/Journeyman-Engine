// Collected when something tagged `tag` touches it: adds `amount` to the
// session number `key` (GameState), plays `sound`, spawns `effect`, tells the
// collector ("pickup", with the key and amount) and is gone.
import { Audio, Entity, GameState, Params, self, spawn } from "@jm/runtime";

const me = self();
const tag = Params.text("tag", "player");  // "": anything
const key = Params.text("key", "coins");
const amount = Params.number("amount", 1);
const sound = Params.text("sound", "");
const effect = Params.text("effect", "");
let taken = false;

export function onOverlapStart(other: Entity): void {
  if (taken || (tag != "" && !other.hasTag(tag))) return;
  taken = true;
  if (key != "") GameState.add(key, amount);
  if (sound != "") Audio.play(sound);
  if (effect != "") spawn(effect, me.transform.x, me.transform.y);
  other.send("pickup", key, amount);
  me.destroy();
}
