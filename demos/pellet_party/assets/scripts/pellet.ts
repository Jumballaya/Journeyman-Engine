// A pellet, simulated by the host: it spins, and goes to whoever's machine
// says they touched it first.
import { GameState, Message, Params, self } from "@jm/runtime";
import { PHASE, scoreKey } from "./lib/players";

const me = self();
const value = Params.number("value", 1);
let eaten = false;

export function onUpdate(dt: f32): void {
  me.transform.rotation += dt * 1.5;
}

export function onMessage(message: Message): void {
  if (message.name != "eat" || eaten || GameState.getString(PHASE) != "play") return;
  eaten = true;
  GameState.add(scoreKey(message.player), value);
  me.destroy();
}
