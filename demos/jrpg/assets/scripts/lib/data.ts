// The game's content: party members, skills and items here; enemies and
// encounters from assets/data/bestiary.json. lib/battle.ts gives it meaning.
import { Data, JsonValue } from "@jm/runtime";

export enum Element { None, Fire, Ice, Bolt }
export enum Effect { Damage, Heal, Restore, Revive, Cure, Sleep, Poison }
export enum Aim { Enemy, Enemies, Ally, Allies, Fallen }  // Fallen: a knocked-out ally

export class Skill {
  name: string = "";
  mp: i32 = 0;
  power: f32 = 1;          // multiplier on the attacker's ATK (physical) or MAG (magic)
  aim: Aim = Aim.Enemy;
  effect: Effect = Effect.Damage;
  magic: bool = false;
  element: Element = Element.None;
  partner: string = "";    // a dual tech: this party member acts too, paying the same MP
}

export const ATTACK: Skill = { name: "ATTACK", mp: 0, power: 1, aim: Aim.Enemy };

export class ItemDef {
  name: string = "";
  effect: Effect = Effect.Heal;
  amount: i32 = 0;
  aim: Aim = Aim.Ally;
  price: i32 = 0;
}

export const ITEMS: ItemDef[] = [
  { name: "POTION", effect: Effect.Heal, amount: 60, aim: Aim.Ally, price: 20 },
  { name: "HI-POTION", effect: Effect.Heal, amount: 180, aim: Aim.Ally, price: 80 },
  { name: "ETHER", effect: Effect.Restore, amount: 25, aim: Aim.Ally, price: 60 },
  { name: "ANTIDOTE", effect: Effect.Cure, amount: 0, aim: Aim.Ally, price: 10 },
  { name: "PHOENIX", effect: Effect.Revive, amount: 30, aim: Aim.Fallen, price: 120 },
];

export function itemNamed(name: string): ItemDef | null {
  for (let i = 0; i < ITEMS.length; i++) if (ITEMS[i].name == name) return ITEMS[i];
  return null;
}

// Stats at level 1 and their gain per level.
export class Growth {
  hp: i32 = 0; mp: i32 = 0; atk: i32 = 0; def: i32 = 0; mag: i32 = 0; spd: i32 = 0;
}

export class HeroDef {
  id: string = "";
  name: string = "";
  base: Growth = new Growth();
  gain: Growth = new Growth();
  skills: Skill[] = [];
}

const FLAME_BLADE: Skill = { name: "FLAME BLADE", mp: 6, power: 2.6, aim: Aim.Enemies, element: Element.Fire, partner: "lyra" };

export const HEROES: HeroDef[] = [
  { id: "kael", name: "KAEL",
    base: { hp: 62, mp: 10, atk: 15, def: 10, mag: 4, spd: 9 }, gain: { hp: 13, mp: 2, atk: 3, def: 2, mag: 1, spd: 1 },
    skills: [
      { name: "POWER STRIKE", mp: 3, power: 1.9, aim: Aim.Enemy },
      { name: "CYCLONE", mp: 6, power: 1.1, aim: Aim.Enemies },
      FLAME_BLADE,
    ] },
  { id: "lyra", name: "LYRA",
    base: { hp: 40, mp: 26, atk: 6, def: 6, mag: 15, spd: 11 }, gain: { hp: 8, mp: 5, atk: 1, def: 1, mag: 3, spd: 1 },
    skills: [
      { name: "FIRE", mp: 4, power: 2.0, aim: Aim.Enemy, magic: true, element: Element.Fire },
      { name: "ICE", mp: 4, power: 2.0, aim: Aim.Enemy, magic: true, element: Element.Ice },
      { name: "BOLT STORM", mp: 9, power: 1.4, aim: Aim.Enemies, magic: true, element: Element.Bolt },
      { name: "SLEEP", mp: 5, power: 0, aim: Aim.Enemy, effect: Effect.Sleep, magic: true },
    ] },
  { id: "bram", name: "BRAM",
    base: { hp: 50, mp: 20, atk: 10, def: 9, mag: 12, spd: 8 }, gain: { hp: 10, mp: 4, atk: 2, def: 2, mag: 2, spd: 1 },
    skills: [
      { name: "HEAL", mp: 4, power: 2.4, aim: Aim.Ally, effect: Effect.Heal, magic: true },
      { name: "HEAL ALL", mp: 10, power: 1.5, aim: Aim.Allies, effect: Effect.Heal, magic: true },
      { name: "CURE", mp: 2, power: 0, aim: Aim.Ally, effect: Effect.Cure, magic: true },
      { name: "RAISE", mp: 12, power: 0.3, aim: Aim.Fallen, effect: Effect.Revive, magic: true },
    ] },
];

export function heroById(id: string): HeroDef {
  for (let i = 0; i < HEROES.length; i++) if (HEROES[i].id == id) return HEROES[i];
  return HEROES[0];
}

// XP needed to go from `level` to the next: 20, 50, 100, 170, 260...
export function xpToNext(level: i32): i32 { return 10 * level * level + 10; }

export class EnemyDef {
  name: string = "";
  sprite: string = "";
  hp: i32 = 0; atk: i32 = 0; def: i32 = 0; mag: i32 = 0; spd: i32 = 0;
  weak: Element = Element.None;
  xp: i32 = 0;
  gold: i32 = 0;
  moves: Skill[] = [];     // chosen at random; ATTACK when empty
  boss: bool = false;
}

// What enemies can do, by name (the bestiary lists moves by name).
const ENEMY_SKILLS: Skill[] = [
  ATTACK,
  { name: "POISON BITE", mp: 0, power: 0.9, aim: Aim.Enemy, effect: Effect.Poison },
  { name: "SPARK", mp: 0, power: 1.4, aim: Aim.Enemy, magic: true, element: Element.Fire },
  { name: "LULLABY", mp: 0, power: 0, aim: Aim.Enemy, effect: Effect.Sleep, magic: true },
  { name: "CINDER BREATH", mp: 0, power: 1.1, aim: Aim.Enemies, magic: true, element: Element.Fire },
  { name: "CLAW", mp: 0, power: 1.5, aim: Aim.Enemy },
];

function enemySkill(name: string): Skill {
  for (let i = 0; i < ENEMY_SKILLS.length; i++) if (ENEMY_SKILLS[i].name == name) return ENEMY_SKILLS[i];
  return ATTACK;
}

function element(name: string): Element {
  return name == "fire" ? Element.Fire : name == "ice" ? Element.Ice : name == "bolt" ? Element.Bolt : Element.None;
}

// Enemies, random encounter groups and the boss fight: assets/data/bestiary.json.
const BESTIARY = Data.json("bestiary");

function readEnemy(json: JsonValue): EnemyDef {
  const e = new EnemyDef();
  e.name = json.get("name").text();
  e.sprite = json.get("sprite").text();
  e.hp = json.get("hp").int();
  e.atk = json.get("atk").int();
  e.def = json.get("def").int();
  e.mag = json.get("mag").int();
  e.spd = json.get("spd").int();
  e.weak = element(json.get("weak").text());
  e.xp = json.get("xp").int();
  e.gold = json.get("gold").int();
  const moves = json.get("moves").strings();
  for (let i = 0; i < moves.length; i++) e.moves.push(enemySkill(moves[i]));
  e.boss = json.get("boss").bool();
  return e;
}

function readGroups(json: JsonValue): string[][] {
  const groups = new Array<string[]>();
  for (let i = 0; i < json.length; i++) groups.push(json.at(i).strings());
  return groups;
}

function readEnemies(json: JsonValue): EnemyDef[] {
  const out = new Array<EnemyDef>();
  for (let i = 0; i < json.length; i++) out.push(readEnemy(json.at(i)));
  return out;
}

export const ENEMIES: EnemyDef[] = readEnemies(BESTIARY.get("enemies"));

// The enemy with this name; a blank one if the bestiary has none.
export function enemyNamed(name: string): EnemyDef {
  for (let i = 0; i < ENEMIES.length; i++) if (ENEMIES[i].name == name) return ENEMIES[i];
  return new EnemyDef();
}

// Random encounters in the Emberwood: one group is picked at random.
export const ENCOUNTERS: string[][] = readGroups(BESTIARY.get("encounters"));
export const BOSS_ENCOUNTER: string[] = BESTIARY.get("boss").strings();
