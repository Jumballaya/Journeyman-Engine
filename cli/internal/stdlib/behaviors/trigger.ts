// A zone (it needs a collider) that acts when something tagged `tag` enters:
// sends `message` (to what's named `target`, else to what entered), spawns the
// scene's `group`, spawns `prefab` where it is, and loads `scene`.
import { Entity, Params, Scene, World, self, spawn } from "@jm/runtime";

const me = self();
const tag = Params.text("tag", "player");  // "": anything
const message = Params.text("message", "");
const target = Params.text("target", "");
const group = Params.text("group", "");
const prefab = Params.text("prefab", "");
const scene = Params.text("scene", "");
const once = Params.number("once", 1) != 0;  // 1: gone after it acts
let used = false;

export function onOverlapStart(other: Entity): void {
  if (used || (tag != "" && !other.hasTag(tag))) return;
  if (message != "") {
    if (target != "") World.broadcast(target, message);
    else other.send(message);
  }
  if (group != "") Scene.spawnGroup(group);
  if (prefab != "") spawn(prefab, me.transform.x, me.transform.y);
  if (scene != "") Scene.load(scene);
  if (!once) return;
  used = true;
  me.destroy();
}
