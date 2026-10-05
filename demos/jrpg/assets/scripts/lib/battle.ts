// The rules of a battle (active-time, Chrono Trigger style): gauges fill by
// speed; a full gauge means a turn. Heroes wait for a command, enemies act on
// their own. Every action resolves at once into a Turn the scene can play
// back. No drawing, input or sound.
import { Random } from "@jm/runtime";
import { ATTACK, Aim, EnemyDef, Effect, Element, ItemDef, Skill } from "./data";
import { Inventory } from "./party";

export const ATB_FULL: f32 = 100;
const ATB_PER_SPEED: f32 = 3.2;   // gauge per second per point of speed: ~3s turns at speed 10
const SLEEP_TURNS: i32 = 3;

export class Fighter {
  id: string = "";
  name: string = "";
  sprite: string = "";
  hero: bool = false;
  boss: bool = false;
  maxHp: i32 = 1; maxMp: i32 = 0; atk: i32 = 1; def: i32 = 0; mag: i32 = 0; spd: i32 = 1;
  hp: i32 = 1;
  mp: i32 = 0;
  weak: Element = Element.None;
  skills: Skill[] = [];
  atb: f32 = 0;
  sleep: i32 = 0;           // turns left asleep
  poisoned: bool = false;
  defending: bool = false;
  xp: i32 = 0;               // rewards for defeating an enemy
  gold: i32 = 0;

  get alive(): bool { return this.hp > 0; }
  get ready(): bool { return this.alive && this.sleep == 0 && this.atb >= ATB_FULL; }

  static enemy(def: EnemyDef): Fighter {
    const f = new Fighter();
    f.id = def.name;
    f.name = def.name;
    f.sprite = def.sprite;
    f.boss = def.boss;
    f.maxHp = f.hp = def.hp;
    f.atk = def.atk; f.def = def.def; f.mag = def.mag; f.spd = def.spd;
    f.weak = def.weak;
    f.skills = def.moves.length > 0 ? def.moves : [ATTACK];
    f.xp = def.xp;
    f.gold = def.gold;
    return f;
  }
}

export enum HitKind { Damage, Heal, Mana, Revive, Status, Miss }

// One effect of a turn on one fighter, for the scene to show.
export class Hit {
  constructor(readonly target: Fighter, readonly kind: HitKind, readonly amount: i32 = 0,
              readonly label: string = "", readonly critical: bool = false) {}
}

export class Turn {
  hits: Hit[] = [];
  partner: Fighter | null = null;  // the second hero of a dual tech
  // `skill` is null for items, defending and running.
  constructor(readonly actor: Fighter, readonly name: string, readonly skill: Skill | null = null) {}
}

export enum Command { Fight, Skill, Item, Defend, Run }
export enum Outcome { Ongoing, Won, Lost, Fled }

export class Battle {
  outcome: Outcome = Outcome.Ongoing;

  constructor(readonly heroes: Fighter[], readonly foes: Fighter[], readonly inventory: Inventory) {
    for (let i = 0; i < heroes.length; i++) heroes[i].atb = Random.range(30, 80);
    for (let i = 0; i < foes.length; i++) foes[i].atb = Random.range(0, 50);
  }

  get canRun(): bool {
    for (let i = 0; i < this.foes.length; i++) if (this.foes[i].boss) return false;
    return true;
  }

  // Fills gauges. A sleeper's full gauge spends a turn asleep instead.
  tick(dt: f32): void {
    const all = this.heroes.concat(this.foes);
    for (let i = 0; i < all.length; i++) {
      const f = all[i];
      if (!f.alive || f.atb >= ATB_FULL) continue;
      f.atb = Mathf.min(ATB_FULL, f.atb + <f32>f.spd * ATB_PER_SPEED * dt);
      if (f.atb >= ATB_FULL && f.sleep > 0) {
        f.sleep--;
        f.atb = 0;
      }
    }
  }

  // The first hero waiting for a command, or null.
  readyHero(): Fighter | null {
    for (let i = 0; i < this.heroes.length; i++) if (this.heroes[i].ready) return this.heroes[i];
    return null;
  }

  // Resolves the next ready enemy's move, or null if none is ready.
  enemyTurn(): Turn | null {
    for (let i = 0; i < this.foes.length; i++) {
      const foe = this.foes[i];
      if (!foe.ready) continue;
      const move = foe.skills[Random.int(0, foe.skills.length - 1)];
      return this.resolve(foe, move, move.aim == Aim.Enemies ? null : this.randomAlive(this.heroes));
    }
    return null;
  }

  // Whether `actor` can use `skill` now (MP, and a ready partner for dual techs).
  canUse(actor: Fighter, skill: Skill): bool {
    if (actor.mp < skill.mp) return false;
    if (skill.partner.length == 0) return true;
    const partner = this.heroNamed(skill.partner);
    return partner !== null && partner.ready && partner.mp >= skill.mp;
  }

  // Who `aim` can target for `actor`'s side; empty when nobody fits.
  targets(actor: Fighter, aim: Aim): Fighter[] {
    const enemies = actor.hero ? this.foes : this.heroes;
    const allies = actor.hero ? this.heroes : this.foes;
    const out = new Array<Fighter>();
    const pool = aim == Aim.Enemy || aim == Aim.Enemies ? enemies : allies;
    for (let i = 0; i < pool.length; i++) {
      if (pool[i].alive != (aim == Aim.Fallen)) out.push(pool[i]);
    }
    return out;
  }

  // A hero's command. `target` is ignored for commands that hit a whole side.
  act(actor: Fighter, command: Command, skill: Skill = ATTACK, item: ItemDef | null = null,
      target: Fighter | null = null): Turn {
    actor.defending = false;
    if (command == Command.Defend) {
      actor.defending = true;
      return this.finish(actor, new Turn(actor, "DEFEND"));
    }
    if (command == Command.Run) return this.run(actor);
    if (command == Command.Item && item !== null) return this.useItem(actor, item!, target);
    return this.resolve(actor, command == Command.Skill ? skill : ATTACK, target);
  }

  get xp(): i32 {
    let total = 0;
    for (let i = 0; i < this.foes.length; i++) total += this.foes[i].xp;
    return total;
  }

  get gold(): i32 {
    let total = 0;
    for (let i = 0; i < this.foes.length; i++) total += this.foes[i].gold;
    return total;
  }

  // ---- Resolution ------------------------------------------------------------

  private resolve(actor: Fighter, skill: Skill, target: Fighter | null): Turn {
    const turn = new Turn(actor, skill.name, skill);
    this.poisonTick(actor, turn);
    actor.mp -= skill.mp;
    if (skill.partner.length > 0) {
      const partner = this.heroNamed(skill.partner)!;
      partner.mp -= skill.mp;
      partner.atb = 0;
      turn.partner = partner;
    }
    const targets = this.aimAt(actor, skill.aim, target);
    for (let i = 0; i < targets.length; i++) this.apply(actor, skill, targets[i], turn);
    return this.finish(actor, turn);
  }

  private apply(actor: Fighter, skill: Skill, target: Fighter, turn: Turn): void {
    if (skill.effect == Effect.Heal) {
      turn.hits.push(this.heal(target, <i32>(<f32>actor.mag * skill.power * 2 * Random.range(0.9, 1.1))));
    } else if (skill.effect == Effect.Revive) {
      turn.hits.push(this.revive(target, skill.power));
    } else if (skill.effect == Effect.Cure) {
      turn.hits.push(this.cure(target));
    } else if (skill.effect == Effect.Sleep) {
      const sleeps = !target.boss && Random.chance(0.75);
      if (sleeps) target.sleep = SLEEP_TURNS;
      turn.hits.push(new Hit(target, sleeps ? HitKind.Status : HitKind.Miss, 0, sleeps ? "SLEEP" : "MISS"));
    } else {
      turn.hits.push(this.damage(actor, skill, target));
      if (skill.effect == Effect.Poison && target.alive && Random.chance(0.5)) {
        target.poisoned = true;
        turn.hits.push(new Hit(target, HitKind.Status, 0, "POISON"));
      }
    }
  }

  private damage(actor: Fighter, skill: Skill, target: Fighter): Hit {
    const power = <f32>(skill.magic ? actor.mag : actor.atk) * skill.power * 2;
    const guard = <f32>target.def * (skill.magic ? 0.5 : 1);
    let amount = Mathf.max(1, power - guard) * Random.range(0.9, 1.1);
    const critical = !skill.magic && Random.chance(0.08);
    if (critical) amount *= 1.5;
    if (skill.element != Element.None && skill.element == target.weak) amount *= 1.5;
    if (target.defending) amount *= 0.5;
    const hp = <i32>amount;
    target.hp = max(0, target.hp - hp);
    target.sleep = 0;  // hits wake sleepers
    return new Hit(target, HitKind.Damage, hp, "", critical);
  }

  private heal(target: Fighter, amount: i32): Hit {
    const healed = min(amount, target.maxHp - target.hp);
    target.hp += healed;
    return new Hit(target, HitKind.Heal, healed);
  }

  private revive(target: Fighter, fraction: f32): Hit {
    if (target.alive) return new Hit(target, HitKind.Miss, 0, "MISS");
    target.hp = max(1, <i32>(<f32>target.maxHp * fraction));
    target.atb = 0;
    return new Hit(target, HitKind.Revive, target.hp, "REVIVED");
  }

  private cure(target: Fighter): Hit {
    target.poisoned = false;
    target.sleep = 0;
    return new Hit(target, HitKind.Status, 0, "CURED");
  }

  private useItem(actor: Fighter, item: ItemDef, target: Fighter | null): Turn {
    const turn = new Turn(actor, item.name);
    this.poisonTick(actor, turn);
    const t = this.aimAt(actor, item.aim, target);
    if (t.length > 0 && this.inventory.take(item.name)) {
      const who = t[0];
      if (item.effect == Effect.Heal) turn.hits.push(this.heal(who, item.amount));
      else if (item.effect == Effect.Restore) {
        const restored = min(item.amount, who.maxMp - who.mp);
        who.mp += restored;
        turn.hits.push(new Hit(who, HitKind.Mana, restored));
      } else if (item.effect == Effect.Revive) turn.hits.push(this.revive(who, <f32>item.amount / 100));
      else if (item.effect == Effect.Cure) turn.hits.push(this.cure(who));
    }
    return this.finish(actor, turn);
  }

  private run(actor: Fighter): Turn {
    const turn = new Turn(actor, "RUN");
    const odds: f32 = 0.55 + (this.averageSpeed(this.heroes) - this.averageSpeed(this.foes)) * 0.04;
    if (this.canRun && Random.chance(odds)) this.outcome = Outcome.Fled;
    else turn.hits.push(new Hit(actor, HitKind.Miss, 0, "CAN'T ESCAPE"));
    return this.finish(actor, turn);
  }

  // Poison bites at the start of the poisoned fighter's turn.
  private poisonTick(actor: Fighter, turn: Turn): void {
    if (!actor.poisoned) return;
    const hp = min(actor.hp - 1, max(1, actor.maxHp / 16));  // poison never knocks out
    actor.hp -= hp;
    turn.hits.push(new Hit(actor, HitKind.Damage, hp, "POISON"));
  }

  private finish(actor: Fighter, turn: Turn): Turn {
    actor.atb = 0;
    if (this.outcome == Outcome.Ongoing) {
      if (this.randomAlive(this.foes) === null) this.outcome = Outcome.Won;
      else if (this.randomAlive(this.heroes) === null) this.outcome = Outcome.Lost;
    }
    return turn;
  }

  // The fighters a move lands on: the chosen one (or another valid one if it
  // fell meanwhile), or the whole side.
  private aimAt(actor: Fighter, aim: Aim, chosen: Fighter | null): Fighter[] {
    const valid = this.targets(actor, aim);
    if (aim == Aim.Enemies || aim == Aim.Allies) return valid;
    if (chosen !== null && valid.includes(chosen!)) return [chosen!];
    return valid.length > 0 ? [valid[Random.int(0, valid.length - 1)]] : [];
  }

  private randomAlive(side: Fighter[]): Fighter | null {
    const alive = side.filter((f: Fighter): bool => f.alive);
    return alive.length == 0 ? null : alive[Random.int(0, alive.length - 1)];
  }

  private heroNamed(id: string): Fighter | null {
    for (let i = 0; i < this.heroes.length; i++) if (this.heroes[i].id == id) return this.heroes[i];
    return null;
  }

  private averageSpeed(side: Fighter[]): f32 {
    let total: f32 = 0;
    for (let i = 0; i < side.length; i++) total += <f32>side[i].spd;
    return total / <f32>max(1, side.length);
  }
}
