// The party and everything that persists between scenes: levels, XP, HP and
// MP, gold, items, story flags and where the party stands. Kept in GameState
// (survives scene changes); save() / load() copy it to the save file.
import { GameState, Save, Store } from "@jm/runtime";
import { HEROES, ITEMS, ItemDef, heroById, xpToNext } from "./data";
import { Fighter } from "./battle";

// GameState keys that belong to the moment, not the journey: never saved.
const TRANSIENT = ["encounter", "arrive"];

export class Inventory {
  count(name: string): i32 { return <i32>GameState.getNumber("item." + name); }
  add(name: string, n: i32 = 1): void { GameState.setNumber("item." + name, min(this.count(name) + n, 99)); }

  // Uses one; false if there is none.
  take(name: string): bool {
    const n = this.count(name);
    if (n == 0) return false;
    GameState.setNumber("item." + name, n - 1);
    return true;
  }

  // The items held, in ITEMS order.
  owned(): ItemDef[] {
    return ITEMS.filter((item: ItemDef): bool => new Inventory().count(item.name) > 0);
  }
}

function key(id: string, field: string): string { return id + "." + field; }

// Adds to a stored HP/MP; an absent one means full and stays full.
function raise(k: string, by: i32): void {
  if (GameState.has(k)) GameState.add(k, by);
}

export class Party {
  static readonly inventory: Inventory = new Inventory();

  static get started(): bool { return GameState.has(key(HEROES[0].id, "level")); }

  static newGame(): void {
    GameState.clear();
    for (let i = 0; i < HEROES.length; i++) {
      GameState.setNumber(key(HEROES[i].id, "level"), 1);
      GameState.setNumber(key(HEROES[i].id, "xp"), 0);
    }
    Party.healAll();
    GameState.setNumber("gold", 40);
    Party.inventory.add("POTION", 3);
    Party.inventory.add("ANTIDOTE", 1);
  }

  static level(id: string): i32 { return <i32>GameState.getNumber(key(id, "level"), 1); }

  // A battle-ready fighter for each hero, from their level and current HP/MP.
  static fighters(): Fighter[] {
    const out = new Array<Fighter>();
    for (let i = 0; i < HEROES.length; i++) {
      const def = HEROES[i];
      const l = Party.level(def.id) - 1;
      const f = new Fighter();
      f.id = def.id;
      f.name = def.name;
      f.sprite = def.id;
      f.hero = true;
      f.maxHp = def.base.hp + def.gain.hp * l;
      f.maxMp = def.base.mp + def.gain.mp * l;
      f.atk = def.base.atk + def.gain.atk * l;
      f.def = def.base.def + def.gain.def * l;
      f.mag = def.base.mag + def.gain.mag * l;
      f.spd = def.base.spd + def.gain.spd * l;
      f.skills = def.skills;
      f.hp = <i32>GameState.getNumber(key(def.id, "hp"), <f64>f.maxHp);
      f.mp = <i32>GameState.getNumber(key(def.id, "mp"), <f64>f.maxMp);
      f.poisoned = GameState.getBool(key(def.id, "poisoned"));
      out.push(f);
    }
    return out;
  }

  // Keeps HP, MP and poison after a battle.
  static keep(fighters: Fighter[]): void {
    for (let i = 0; i < fighters.length; i++) {
      const f = fighters[i];
      GameState.setNumber(key(f.id, "hp"), f.hp);
      GameState.setNumber(key(f.id, "mp"), f.mp);
      GameState.setBool(key(f.id, "poisoned"), f.poisoned);
    }
  }

  // Shares XP among the standing heroes; returns "KAEL REACHED LEVEL 3!" lines.
  static gainXp(fighters: Fighter[], xp: i32): string[] {
    const news = new Array<string>();
    for (let i = 0; i < fighters.length; i++) {
      const f = fighters[i];
      if (!f.alive) continue;
      let level = Party.level(f.id);
      let total = <i32>GameState.getNumber(key(f.id, "xp")) + xp;
      while (total >= xpToNext(level)) {
        total -= xpToNext(level);
        level++;
        news.push(f.name + " REACHED LEVEL " + level.toString() + "!");
        const gain = heroById(f.id).gain;
        raise(key(f.id, "hp"), gain.hp);  // the new points arrive filled
        raise(key(f.id, "mp"), gain.mp);
      }
      GameState.setNumber(key(f.id, "level"), level);
      GameState.setNumber(key(f.id, "xp"), total);
    }
    return news;
  }

  // Full HP and MP, statuses cleared, the fallen revived (the inn).
  static healAll(): void {
    for (let i = 0; i < HEROES.length; i++) {
      GameState.remove(key(HEROES[i].id, "hp"));  // absent = full
      GameState.remove(key(HEROES[i].id, "mp"));
      GameState.remove(key(HEROES[i].id, "poisoned"));
    }
  }

  static get gold(): i32 { return <i32>GameState.getNumber("gold"); }
  static set gold(n: i32) { GameState.setNumber("gold", max(0, n)); }

  // One-time story events: chests opened, the wyrm beaten.
  static done(flag: string): bool { return GameState.getStrings("flags").includes(flag); }
  static markDone(flag: string): void {
    const flags = GameState.getStrings("flags");
    if (flags.includes(flag)) return;
    flags.push(flag);
    GameState.setStrings("flags", flags);
  }

  // Where the party stands on a map, for returning from battles and loading saves.
  static get map(): string { return GameState.getString("map", "town"); }
  static get x(): f32 { return <f32>GameState.getNumber("x", -1); }
  static get y(): f32 { return <f32>GameState.getNumber("y", -1); }
  static placeAt(map: string, x: f32, y: f32): void {
    GameState.setString("map", map);
    GameState.setNumber("x", x);
    GameState.setNumber("y", y);
    GameState.remove("arrive");
  }
  // Next on `map`, beside its exit tile `exit` (where the party walks in).
  static arriveAt(map: string, exit: string): void {
    Party.placeAt(map, -1, -1);
    GameState.setString("arrive", exit);
  }
  static get arrivingBy(): string { return GameState.getString("arrive"); }

  static get hasSave(): bool { return Save.has(key(HEROES[0].id, "level")); }

  static save(): void { copy(GameState, Save); }
  static load(): void { copy(Save, GameState); }
}

// Replaces `to` with everything in `from` except the transient keys.
function copy(from: Store, to: Store): void {
  to.clear();
  const keys = from.keys();
  for (let i = 0; i < keys.length; i++) {
    if (!TRANSIENT.includes(keys[i])) to.setJson(keys[i], from.getJson(keys[i]));
  }
}
