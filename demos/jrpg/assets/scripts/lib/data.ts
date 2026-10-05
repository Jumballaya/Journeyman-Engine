// The game's content: party members, skills, items, enemies and encounters.
// Plain data; lib/battle.ts gives it meaning.

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

const BITE: Skill = { name: "POISON BITE", mp: 0, power: 0.9, aim: Aim.Enemy, effect: Effect.Poison };
const SPARK: Skill = { name: "SPARK", mp: 0, power: 1.4, aim: Aim.Enemy, magic: true, element: Element.Fire };
const LULL: Skill = { name: "LULLABY", mp: 0, power: 0, aim: Aim.Enemy, effect: Effect.Sleep, magic: true };
const BREATH: Skill = { name: "CINDER BREATH", mp: 0, power: 1.1, aim: Aim.Enemies, magic: true, element: Element.Fire };
const CLAW: Skill = { name: "CLAW", mp: 0, power: 1.5, aim: Aim.Enemy };

export const ENEMIES: EnemyDef[] = [
  { name: "JELLY", sprite: "jelly", hp: 30, atk: 11, def: 4, mag: 4, spd: 6, weak: Element.Fire, xp: 14, gold: 5 },
  { name: "GOBLIN", sprite: "goblin", hp: 46, atk: 15, def: 8, mag: 2, spd: 10, xp: 22, gold: 10, moves: [ATTACK, ATTACK, BITE] },
  { name: "WISP", sprite: "wisp", hp: 30, atk: 6, def: 5, mag: 14, spd: 13, weak: Element.Ice, xp: 24, gold: 8, moves: [SPARK, SPARK, LULL] },
  { name: "CINDER WYRM", sprite: "wyrm", hp: 620, atk: 24, def: 14, mag: 21, spd: 12, weak: Element.Ice, xp: 800, gold: 300, moves: [CLAW, CLAW, BREATH], boss: true },
];

export function enemyNamed(name: string): EnemyDef {
  for (let i = 0; i < ENEMIES.length; i++) if (ENEMIES[i].name == name) return ENEMIES[i];
  return ENEMIES[0];
}

// Random encounters in the Emberwood: one group is picked at random.
export const ENCOUNTERS: string[][] = [
  ["JELLY", "JELLY"],
  ["JELLY", "JELLY", "JELLY"],
  ["GOBLIN"],
  ["GOBLIN", "JELLY"],
  ["WISP", "JELLY"],
  ["GOBLIN", "WISP"],
  ["WISP", "WISP", "GOBLIN"],
];
export const BOSS_ENCOUNTER: string[] = ["CINDER WYRM"];
