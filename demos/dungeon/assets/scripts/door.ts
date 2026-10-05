// A locked door tile. The hero opens it (Session.markDone of param "place");
// the door then vanishes.
import { Params, spawn, self } from "@jm/runtime";
import { Session } from "./lib/session";

const me = self();
const place = Params.text("place");

export function onUpdate(dt: f32): void {
  if (!Session.done(place)) return;
  spawn("poof", me.transform.x, me.transform.y);
  me.destroy();
}
