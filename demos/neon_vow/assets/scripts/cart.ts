import { GameState, Message, Path, TileMap, Vec2, World, self } from "@jm/runtime";
import { CartPhase, CartRun, physicsStep } from "./lib/cart";

const me = self();
const rail = Path.fromObject(TileMap.find("Map").objects("rail")[0]);
const ride = new CartRun();
const at = new Vec2();
let seconds: f32 = 0;

export function onMessage(message: Message): void {
  if (message.name == "reset") recall();
}

function recall(): void {
  ride.reset();
  seconds = 0;
  rail.at(0, at);
  me.transform.setPosition(at.x, at.y);
}

export function onUpdate(frameDt: f32): void {
  const dt = physicsStep(frameDt);
  seconds += dt;
  const hero = World.find("Kage");
  const aboard = hero.velocity.floor.equals(me);
  rail.at(0, at);
  // Back on the station without it: the sled returns to wait there.
  if (ride.phase != CartPhase.Waiting && !aboard && hero.velocity.onGround && hero.transform.x < at.x) recall();
  ride.tick(frameDt, rail.length, aboard);
  rail.at(ride.along, at);
  // The solid deck bobs with the hull: move() carries the rider in both axes.
  at.y += Mathf.sin(seconds * 3) * 0.8;
  const dx = at.x - me.transform.x, dy = at.y - me.transform.y;
  me.move(dx, dy);
  me.data.setNumber("vx", dx / dt);
  me.data.setNumber("vy", dy / dt);
  me.particles.emitting = ride.speed > 20;
  me.child("Wake").particles.emitting = ride.speed > 20;
  GameState.setNumber("cartPhase", ride.phase);
  GameState.setNumber("cartProgress", ride.along);
  GameState.setNumber("cartSpeed", ride.speed);
  GameState.setBool("cartArrived", ride.phase == CartPhase.Parked);
}
