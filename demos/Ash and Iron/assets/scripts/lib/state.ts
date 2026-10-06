// The hero's story so far, kept in GameState so it survives scene changes and
// goes into save slots whole (slots.ts copies every key):
//   hero.hp hero.xp hero.scrip        inv.<item> = count      eq.weapon eq.armor
//   learn.<ability> = 1               q.<quest> = step (0 none, DONE finished)
//   flag.<name> = 1                   gone.<scene>.<entity> (taken, killed)
// Conditions and actions are the little language the dialogue tables use:
//   when:   q:iron_warden>=2, q:supply_run=done, has:locket, !flag:mend, scrip>=10
//   action: start:q advance:q complete:q give:item take:item learn:ability flag:x pay:n heal shop
import { GameState } from "@jm/runtime";
import { ITEMS, QUESTS, ability, item, levelFor, quest, stage, stageCount } from "./db";

export const DONE: i32 = 99;

// What happened, for the HUD to show (drained by the game controller).
export class Note {
  constructor(public text: string, public sound: string) {}
}
export const NOTES: Note[] = [];
function note(text: string, sound: string = ""): void { NOTES.push(new Note(text, sound)); }

// ---- the hero -------------------------------------------------------------------

export function level(): i32 { return levelFor(xp()).level; }
export function maxHp(): i32 { return levelFor(xp()).hp; }
export function maxAp(): i32 { return levelFor(xp()).ap; }
export function xp(): i32 { return <i32>GameState.getNumber("hero.xp"); }
export function hp(): i32 { return <i32>GameState.getNumber("hero.hp", maxHp()); }
export function scrip(): i32 { return <i32>GameState.getNumber("hero.scrip"); }

export function setHp(value: i32): void { GameState.setNumber("hero.hp", min(maxHp(), max(0, value))); }
export function heal(amount: i32): void { setHp(hp() + amount); }
export function addScrip(amount: i32): void { GameState.setNumber("hero.scrip", max(0, scrip() + amount)); }

// Experience; a new level heals and says so.
export function gainXp(amount: i32): void {
  if (amount <= 0) return;
  const before = level();
  GameState.setNumber("hero.xp", xp() + amount);
  note("+" + amount.toString() + " XP");
  if (level() > before) {
    setHp(maxHp());
    note("Level " + level().toString() + "! HP " + maxHp().toString() + ", AP " + maxAp().toString(), "level_up");
  }
}

// A new game: everything cleared, the hero at full health with a little money.
export function newGame(): void {
  GameState.clear();
  GameState.setNumber("hero.xp", 0);
  GameState.setNumber("hero.hp", maxHp());
  GameState.setNumber("hero.scrip", 25);
  GameState.setNumber("inv.tonic", 2);
  GameState.setNumber("inv.bandage", 1);
  GameState.setNumber("learn.strike", 1);
}

// ---- items ----------------------------------------------------------------------

export function count(id: string): i32 { return <i32>GameState.getNumber("inv." + id); }
export function has(id: string): bool { return count(id) > 0 || equipped("weapon") == id || equipped("armor") == id; }

export function give(id: string, n: i32 = 1): void {
  const def = item(id);
  if (def != null && def.kind == "money") {
    addScrip(def.value * n);
    note("+" + (def.value * n).toString() + " scrip", "pickup");
    return;
  }
  GameState.setNumber("inv." + id, count(id) + n);
  const name = def != null ? def.name : id;
  note("Got " + name + (n > 1 ? " x" + n.toString() : ""), "pickup");
  advanceQuests();
}

export function take(id: string, n: i32 = 1): void {
  const left = count(id) - n;
  if (left > 0) GameState.setNumber("inv." + id, left);
  else GameState.remove("inv." + id);
}

// Item ids carried, in the table's order (stable for a menu).
export function carried(): string[] {
  const out: string[] = [];
  for (let i = 0; i < ITEMS.length; i++) if (count(ITEMS[i].id) > 0) out.push(ITEMS[i].id);
  return out;
}

export function equipped(slot: string): string { return GameState.getString("eq." + slot); }

// Weapons and armor move between the pack and their slot.
export function equip(id: string): void {
  const def = item(id);
  if (def == null || (def.kind != "weapon" && def.kind != "armor")) return;
  const slot = def.kind;
  const current = equipped(slot);
  if (current == id) {  // again: take it off
    GameState.remove("eq." + slot);
    GameState.setNumber("inv." + id, count(id) + 1);
    note("Unequipped " + def.name);
    return;
  }
  take(id);
  if (current.length > 0) GameState.setNumber("inv." + current, count(current) + 1);
  GameState.setString("eq." + slot, id);
  note("Equipped " + def.name);
}

export function weaponBonus(): i32 {
  const w = item(equipped("weapon"));
  return w != null && w.id != "revolver" ? w.power : 0;  // the revolver is for Aimed Shot
}

export function armor(): i32 {
  const a = item(equipped("armor"));
  return a != null ? a.power : 0;
}

// ---- abilities ------------------------------------------------------------------

export function knows(id: string): bool {
  const def = ability(id);
  if (def == null) return false;
  if (def.requires.length > 0 && has(def.requires)) return true;  // the revolver teaches its own shot
  return GameState.getNumber("learn." + id) > 0;
}

// Why an ability can't be used right now ("" if it can).
export function cantUse(id: string, ap: i32): string {
  const def = ability(id);
  if (def == null || !knows(id)) return "Not learned";
  if (def.requires.length > 0 && !has(def.requires)) return "Needs " + itemName(def.requires);
  if (def.consumes.length > 0 && count(def.consumes) <= 0) return "No " + itemName(def.consumes);
  if (ap < def.ap) return "Not enough AP";
  return "";
}

export function itemName(id: string): string {
  const def = item(id);
  return def != null ? def.name : id;
}

// ---- quests ---------------------------------------------------------------------

export function step(id: string): i32 { return <i32>GameState.getNumber("q." + id); }
export function active(id: string): bool { return step(id) > 0 && step(id) != DONE; }

export function startQuest(id: string): void {
  if (step(id) != 0) return;
  GameState.setNumber("q." + id, 1);
  const def = quest(id);
  note("New quest: " + (def != null ? def.name : id), "quest");
  advanceQuests();
}

export function advance(id: string): void {
  if (!active(id)) return;
  const next = step(id) + 1;
  if (next > stageCount(id)) { complete(id); return; }
  GameState.setNumber("q." + id, next);
  const s = stage(id, next);
  if (s != null) note(s.text, "quest");
}

export function complete(id: string): void {
  if (step(id) == DONE) return;
  GameState.setNumber("q." + id, DONE);
  const def = quest(id);
  if (def == null) return;
  note("Quest complete: " + def.name, "quest");
  if (def.scrip > 0) { addScrip(def.scrip); note("+" + def.scrip.toString() + " scrip"); }
  if (def.item.length > 0) give(def.item);
  gainXp(def.xp);
}

// Steps whose `until` condition now holds are finished (several, if several hold).
export function advanceQuests(): void {
  for (let i = 0; i < QUESTS.length; i++) {
    const id = QUESTS[i].id;
    for (let guard = 0; guard < 8 && active(id); guard++) {
      const s = stage(id, step(id));
      if (s == null || s.until.length == 0 || !test(s.until)) break;
      advance(id);
    }
  }
}

// The quest worth showing on the HUD: the main one first, else any open one.
export function trackedQuest(): string {
  if (active("iron_warden")) return "iron_warden";
  for (let i = 0; i < QUESTS.length; i++) if (active(QUESTS[i].id)) return QUESTS[i].id;
  return "";
}

// ---- flags, conditions and actions ----------------------------------------------

export function flag(name: string): bool { return GameState.getNumber("flag." + name) > 0; }
export function setFlag(name: string): void {
  GameState.setNumber("flag." + name, 1);
  advanceQuests();
}

// Something taken or killed in a scene stays gone (scene entries say `unless` it).
export function goneKey(scene: string, entity: string): string { return "gone." + scene + "." + entity; }

function compare(value: i32, op: string, target: i32): bool {
  if (op == ">=") return value >= target;
  if (op == "<=") return value <= target;
  if (op == ">") return value > target;
  if (op == "<") return value < target;
  return value == target;
}

function testOne(term: string): bool {
  let t = term.trim();
  if (t.length == 0 || t == "-") return true;
  const negate = t.startsWith("!");
  if (negate) t = t.substring(1);
  let result = false;
  if (t.startsWith("has:")) result = has(t.substring(4));
  else if (t.startsWith("flag:")) result = flag(t.substring(5));
  else {
    // q:<quest><op><number|done>, or scrip<op><number>
    let op = "";
    let at = -1;
    for (let i = 0; i < t.length; i++) {
      const c = t.charAt(i);
      if (c == ">" || c == "<" || c == "=") { at = i; op = c; if (i + 1 < t.length && t.charAt(i + 1) == "=") op += "="; break; }
    }
    if (at < 0) return false;
    const subject = t.substring(0, at);
    const raw = t.substring(at + op.length);
    const target = raw == "done" ? DONE : I32.parseInt(raw);
    if (subject.startsWith("q:")) result = compare(step(subject.substring(2)), op, target);
    else if (subject == "scrip") result = compare(scrip(), op, target);
    else if (subject == "level") result = compare(level(), op, target);
  }
  return negate ? !result : result;
}

// All of a comma-separated list of conditions hold ("" always holds).
export function test(when: string): bool {
  const terms = when.split(",");
  for (let i = 0; i < terms.length; i++) if (!testOne(terms[i])) return false;
  return true;
}

// Runs a comma-separated list of actions; returns what the caller must open ("shop"), if anything.
export function run(actions: string): string {
  let open = "";
  const list = actions.split(",");
  for (let i = 0; i < list.length; i++) {
    const a = list[i].trim();
    if (a.length == 0 || a == "-") continue;
    const colon = a.indexOf(":");
    const verb = colon < 0 ? a : a.substring(0, colon);
    const arg = colon < 0 ? "" : a.substring(colon + 1);
    if (verb == "start") startQuest(arg);
    else if (verb == "advance") advance(arg);
    else if (verb == "complete") complete(arg);
    else if (verb == "give") give(arg);
    else if (verb == "take") take(arg);
    else if (verb == "learn") {
      GameState.setNumber("learn." + arg, 1);
      const def = ability(arg);
      note("Learned " + (def != null ? def.name : arg), "level_up");
    } else if (verb == "flag") setFlag(arg);
    else if (verb == "pay") addScrip(-I32.parseInt(arg));
    else if (verb == "heal") { setHp(maxHp()); note("Fully healed", "mend"); }
    else if (verb == "shop" || verb == "ending") open = verb;
  }
  return open;
}
