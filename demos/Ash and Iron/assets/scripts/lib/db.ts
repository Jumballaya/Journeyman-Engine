// The game's content, read from the tables in assets/data/ (edited in the
// editor's data editor). Nothing here is game state: see state.ts for that.
import { Data, JsonValue } from "@jm/runtime";

export const ATLAS = "assets/sprites.atlas.json#";

export class ItemDef {
  id: string = "";
  name: string = "";
  kind: string = "";   // consumable, reagent, ammo, weapon, armor, key, quest, money
  icon: string = "";
  value: i32 = 0;
  power: i32 = 0;      // heal (consumables), damage bonus (weapons), block (armor)
  description: string = "";
}

export class AbilityDef {
  id: string = "";
  name: string = "";
  ap: i32 = 0;
  range: i32 = 1;
  power: i32 = 0;
  kind: string = "";      // melee, ranged, ember, heal
  requires: string = "";  // an item the hero must own
  consumes: string = "";  // an item each use spends
  sound: string = "";
  description: string = "";
  icon: string = "";
}

export class EnemyDef {
  id: string = "";
  name: string = "";
  sprite: string = "";
  hp: i32 = 1;
  ap: i32 = 6;
  damage: i32 = 1;
  range: i32 = 1;
  accuracy: i32 = 70;
  armor: i32 = 0;
  xp: i32 = 0;
  loot: string = "";
  ai: string = "melee";
  sound: string = "";
  description: string = "";
}

export class Person {
  id: string = "";
  name: string = "";
  portrait: string = "";
  sprite: string = "";
}

export class Line {
  id: string = "";
  speaker: string = "";
  text: string = "";
  next: string = "";
  action: string = "";
}

export class Choice {
  line: string = "";
  text: string = "";
  goto: string = "";
  when: string = "";
  action: string = "";
}

export class Opening {
  npc: string = "";
  when: string = "";
  line: string = "";
}

export class QuestDef {
  id: string = "";
  name: string = "";
  giver: string = "";
  description: string = "";
  xp: i32 = 0;
  scrip: i32 = 0;
  item: string = "";
}

export class Stage {
  quest: string = "";
  step: i32 = 0;
  text: string = "";
  until: string = "";   // the condition that finishes this step
}

export class Level {
  level: i32 = 1;
  xp: i32 = 0;
  hp: i32 = 30;
  ap: i32 = 8;
}

export class Stock {
  item: string = "";
  price: i32 = 0;
}

function rows(file: string, table: string): JsonValue {
  return Data.json(file).get(table);
}

export const ITEMS: ItemDef[] = [];
export const ABILITIES: AbilityDef[] = [];
export const ENEMIES: EnemyDef[] = [];
export const PEOPLE: Person[] = [];
export const LINES: Line[] = [];
export const CHOICES: Choice[] = [];
export const OPENINGS: Opening[] = [];
export const QUESTS: QuestDef[] = [];
export const STAGES: Stage[] = [];
export const LEVELS: Level[] = [];
export const STOCK: Stock[] = [];

function load(): void {
  let t = rows("items", "items");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new ItemDef();
    d.id = r.get("id").text(); d.name = r.get("name").text(); d.kind = r.get("kind").text();
    d.icon = r.get("icon").text(); d.value = r.get("value").int(); d.power = r.get("power").int();
    d.description = r.get("description").text();
    ITEMS.push(d);
  }
  t = rows("abilities", "abilities");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new AbilityDef();
    d.id = r.get("id").text(); d.name = r.get("name").text(); d.ap = r.get("ap").int(); d.range = r.get("range").int();
    d.power = r.get("power").int(); d.kind = r.get("kind").text(); d.requires = r.get("requires").text();
    d.consumes = r.get("consumes").text(); d.sound = r.get("sound").text(); d.description = r.get("description").text();
    d.icon = r.get("icon").text();
    ABILITIES.push(d);
  }
  t = rows("enemies", "enemies");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new EnemyDef();
    d.id = r.get("id").text(); d.name = r.get("name").text(); d.sprite = r.get("sprite").text();
    d.hp = r.get("hp").int(1); d.ap = r.get("ap").int(6); d.damage = r.get("damage").int(1); d.range = r.get("range").int(1);
    d.accuracy = r.get("accuracy").int(70); d.armor = r.get("armor").int(); d.xp = r.get("xp").int();
    d.loot = r.get("loot").text(); d.ai = r.get("ai").text("melee"); d.sound = r.get("sound").text();
    d.description = r.get("description").text();
    ENEMIES.push(d);
  }
  t = rows("npcs", "npcs");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new Person();
    d.id = r.get("id").text(); d.name = r.get("name").text(); d.portrait = r.get("portrait").text(); d.sprite = r.get("sprite").text();
    PEOPLE.push(d);
  }
  t = rows("dialogue", "lines");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new Line();
    d.id = r.get("id").text(); d.speaker = r.get("speaker").text(); d.text = r.get("text").text();
    d.next = r.get("next").text(); d.action = r.get("action").text();
    LINES.push(d);
  }
  t = rows("dialogue", "choices");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new Choice();
    d.line = r.get("line").text(); d.text = r.get("text").text(); d.goto = r.get("goto").text();
    d.when = r.get("when").text(); d.action = r.get("action").text();
    CHOICES.push(d);
  }
  t = rows("dialogue", "talk");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new Opening();
    d.npc = r.get("npc").text(); d.when = r.get("when").text(); d.line = r.get("line").text();
    OPENINGS.push(d);
  }
  t = rows("quests", "quests");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new QuestDef();
    d.id = r.get("id").text(); d.name = r.get("name").text(); d.giver = r.get("giver").text();
    d.description = r.get("description").text(); d.xp = r.get("reward_xp").int(); d.scrip = r.get("reward_scrip").int();
    d.item = r.get("reward_item").text();
    QUESTS.push(d);
  }
  t = rows("quests", "stages");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new Stage();
    d.quest = r.get("quest").text(); d.step = r.get("step").int(); d.text = r.get("text").text(); d.until = r.get("until").text();
    STAGES.push(d);
  }
  t = rows("progression", "progression");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new Level();
    d.level = r.get("level").int(1); d.xp = r.get("xp").int(); d.hp = r.get("hp").int(30); d.ap = r.get("ap").int(8);
    LEVELS.push(d);
  }
  t = rows("shop", "shop");
  for (let i = 0; i < t.length; i++) {
    const r = t.at(i);
    const d = new Stock();
    d.item = r.get("item").text(); d.price = r.get("price").int();
    STOCK.push(d);
  }
}
load();

export function item(id: string): ItemDef | null {
  for (let i = 0; i < ITEMS.length; i++) if (ITEMS[i].id == id) return ITEMS[i];
  return null;
}

export function ability(id: string): AbilityDef | null {
  for (let i = 0; i < ABILITIES.length; i++) if (ABILITIES[i].id == id) return ABILITIES[i];
  return null;
}

export function enemy(id: string): EnemyDef | null {
  for (let i = 0; i < ENEMIES.length; i++) if (ENEMIES[i].id == id) return ENEMIES[i];
  return null;
}

export function person(id: string): Person | null {
  for (let i = 0; i < PEOPLE.length; i++) if (PEOPLE[i].id == id) return PEOPLE[i];
  return null;
}

export function line(id: string): Line | null {
  for (let i = 0; i < LINES.length; i++) if (LINES[i].id == id) return LINES[i];
  return null;
}

export function quest(id: string): QuestDef | null {
  for (let i = 0; i < QUESTS.length; i++) if (QUESTS[i].id == id) return QUESTS[i];
  return null;
}

export function stage(questId: string, step: i32): Stage | null {
  for (let i = 0; i < STAGES.length; i++) if (STAGES[i].quest == questId && STAGES[i].step == step) return STAGES[i];
  return null;
}

export function stageCount(questId: string): i32 {
  let n = 0;
  for (let i = 0; i < STAGES.length; i++) if (STAGES[i].quest == questId) n = max(n, STAGES[i].step);
  return n;
}

// The level a total of experience reaches.
export function levelFor(xp: i32): Level {
  let best = LEVELS.length > 0 ? LEVELS[0] : new Level();
  for (let i = 0; i < LEVELS.length; i++) if (xp >= LEVELS[i].xp && LEVELS[i].level >= best.level) best = LEVELS[i];
  return best;
}

export function nextLevel(level: i32): Level | null {
  for (let i = 0; i < LEVELS.length; i++) if (LEVELS[i].level == level + 1) return LEVELS[i];
  return null;
}
