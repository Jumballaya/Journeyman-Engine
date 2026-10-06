// Three save slots in the player's save file. A slot is every GameState key
// (state.ts keeps the whole story there) plus a summary for the slot list.
import { GameState, JsonValue, Save } from "@jm/runtime";

export const SLOTS: i32 = 3;

export class Summary {
  used: bool = false;
  place: string = "";
  level: i32 = 0;
  seconds: f64 = 0;
}

function key(slot: i32): string { return "slot" + slot.toString(); }

// Writes the current game; `place` is the location's display name.
export function save(slot: i32, place: string, level: i32): void {
  const state = JsonValue.object();
  const keys = GameState.keys("");
  for (let i = 0; i < keys.length; i++) state.set(keys[i], GameState.getJson(keys[i]));
  const out = JsonValue.object();
  out.set("place", JsonValue.string(place));
  out.set("level", JsonValue.number(level));
  out.set("seconds", JsonValue.number(GameState.getNumber("time.played")));
  out.set("state", state);
  Save.setJson(key(slot), out);
  Save.setNumber("lastSlot", slot);
}

export function summary(slot: i32): Summary {
  const s = new Summary();
  const v = Save.getJson(key(slot));
  if (v.isNull) return s;
  s.used = true;
  s.place = v.get("place").text("?");
  s.level = v.get("level").int(1);
  s.seconds = v.get("seconds").number();
  return s;
}

// Replaces the current game with the slot's; false for an empty slot.
export function load(slot: i32): bool {
  const v = Save.getJson(key(slot));
  if (v.isNull) return false;
  const state = v.get("state");
  GameState.clear();
  const keys = state.keys();
  for (let i = 0; i < keys.length; i++) GameState.setJson(keys[i], state.get(keys[i]));
  Save.setNumber("lastSlot", slot);
  return true;
}

export function anyUsed(): bool {
  for (let s = 1; s <= SLOTS; s++) if (summary(s).used) return true;
  return false;
}

// The slot played most recently (for Continue), or 0.
export function latest(): i32 {
  const last = <i32>Save.getNumber("lastSlot");
  return last > 0 && summary(last).used ? last : 0;
}

export function clock(seconds: f64): string {
  const total = <i32>seconds;
  const m = (total / 60) % 60, h = total / 3600;
  return h.toString() + ":" + (m < 10 ? "0" : "") + m.toString();
}
