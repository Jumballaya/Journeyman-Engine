// A pickup that applies itself when the hero touches it. Param "kind": heart,
// gem, key, container or shard; "place" marks a one-time item as taken.
import { Entity, Params, Sound, self } from "@jm/runtime";
import { Session } from "./lib/session";

const me = self();
const kind = Params.text("kind");
const place = Params.text("place");

function take(): void {
  if (kind == "heart") {
    Session.health = Session.health + 2;
    new Sound("heart").play(0.6);
  } else if (kind == "gem") {
    Session.addGems(1);
    new Sound("gem").play(0.6);
  } else if (kind == "key") {
    Session.keys = Session.keys + 1;
    new Sound("key").play(0.6);
  } else if (kind == "container") {
    Session.addHeartContainer();
    new Sound("item").play(0.6);
  } else if (kind == "shard") {
    Session.won = true;
    new Sound("victory").play(0.7);
  }
  if (place.length > 0) Session.markDone(place);
  me.destroy();
}

export function onCollide(other: Entity): void {
  if (other.hasTag("hero")) take();
}
