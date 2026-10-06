// Rule tests for lib/battle.ts and lib/party.ts; run with `node --test tests/*.test.mjs`.
import { Battle, Command, Fighter, HitKind, Outcome, ATB_FULL } from "../assets/scripts/lib/battle";
import { ENEMIES, ITEMS, Skill, enemyNamed, heroById } from "../assets/scripts/lib/data";
import { Party } from "../assets/scripts/lib/party";

function battle(foes: string[]): Battle {
  Party.newGame();
  const enemies = new Array<Fighter>();
  for (let i = 0; i < foes.length; i++) enemies.push(Fighter.enemy(enemyNamed(foes[i])));
  return new Battle(Party.fighters(), enemies, Party.inventory);
}

function skill(hero: string, name: string): Skill {
  const skills = heroById(hero).skills;
  for (let i = 0; i < skills.length; i++) if (skills[i].name == name) return skills[i];
  return skills[0];
}

export function attackDamagesAndEndsTheTurn(): void {
  const b = battle(["GOBLIN"]);
  const kael = b.heroes[0];
  kael.atb = ATB_FULL;
  const turn = b.act(kael, Command.Fight, skill("kael", ""), null, b.foes[0]);
  assert(turn.hits.length == 1 && turn.hits[0].kind == HitKind.Damage && turn.hits[0].amount > 0);
  assert(b.foes[0].hp == b.foes[0].maxHp - turn.hits[0].amount);
  assert(kael.atb == 0);
}

export function weaknessesHurtMore(): void {
  const b = battle(["JELLY"]);
  const lyra = b.heroes[1];
  let fire = 0, ice = 0;
  for (let i = 0; i < 30; i++) {
    b.foes[0].hp = 999;
    lyra.mp = 99;
    fire += b.act(lyra, Command.Skill, skill("lyra", "FIRE"), null, b.foes[0]).hits[0].amount;
    b.foes[0].hp = 999;
    ice += b.act(lyra, Command.Skill, skill("lyra", "ICE"), null, b.foes[0]).hits[0].amount;
  }
  assert(<f32>fire > <f32>ice * 1.3);  // jellies are weak to fire
}

export function healingStopsAtMaxHp(): void {
  const b = battle(["JELLY"]);
  const kael = b.heroes[0];
  kael.hp = kael.maxHp - 5;
  const turn = b.act(b.heroes[2], Command.Skill, skill("bram", "HEAL"), null, kael);
  assert(turn.hits[0].kind == HitKind.Heal && turn.hits[0].amount == 5 && kael.hp == kael.maxHp);
}

export function sleepersLoseTurnsUntilHit(): void {
  const b = battle(["GOBLIN"]);
  const goblin = b.foes[0];
  goblin.sleep = 3;
  goblin.atb = 0;
  for (let i = 0; i < 100; i++) b.tick(0.1);
  assert(goblin.sleep < 3 && !goblin.ready || goblin.sleep == 0);
  goblin.sleep = 2;
  b.act(b.heroes[0], Command.Fight, skill("kael", ""), null, goblin);
  assert(goblin.sleep == 0);  // hits wake it
}

export function dualTechNeedsAReadyPartner(): void {
  const b = battle(["GOBLIN", "GOBLIN"]);
  const kael = b.heroes[0], lyra = b.heroes[1];
  const blade = skill("kael", "FLAME BLADE");
  kael.mp = 20;
  lyra.mp = 20;
  lyra.atb = 0;
  assert(!b.canUse(kael, blade));
  lyra.atb = ATB_FULL;
  assert(b.canUse(kael, blade));
  const turn = b.act(kael, Command.Skill, blade);
  assert(turn.partner === lyra && turn.hits.length == 2);
  assert(kael.mp == 20 - blade.mp && lyra.mp == 20 - blade.mp && lyra.atb == 0);
}

export function winningPaysOut(): void {
  const b = battle(["JELLY", "JELLY"]);
  b.foes[0].hp = 0;
  b.foes[1].hp = 1;
  b.act(b.heroes[0], Command.Fight, skill("kael", ""), null, b.foes[1]);
  assert(b.outcome == Outcome.Won);
  assert(b.xp == 2 * enemyNamed("JELLY").xp && b.gold == 2 * enemyNamed("JELLY").gold);
}

export function itemsAreUsedUp(): void {
  const b = battle(["JELLY"]);
  const kael = b.heroes[0];
  kael.hp = 10;
  const potion = ITEMS[0];
  const before = Party.inventory.count(potion.name);
  b.act(b.heroes[1], Command.Item, skill("kael", ""), potion, kael);
  assert(Party.inventory.count(potion.name) == before - 1 && kael.hp > 10);
}

export function nobodyRunsFromTheWyrm(): void {
  const b = battle(["CINDER WYRM"]);
  for (let i = 0; i < 20; i++) b.act(b.heroes[0], Command.Run);
  assert(b.outcome == Outcome.Ongoing);
}

export function poisonNeverKnocksOut(): void {
  const b = battle(["GOBLIN"]);
  const kael = b.heroes[0];
  kael.hp = 1;
  kael.poisoned = true;
  b.act(kael, Command.Defend);
  b.act(kael, Command.Fight, skill("kael", ""), null, b.foes[0]);
  assert(kael.hp == 1);
}

export function experienceRaisesLevels(): void {
  const b = battle(["JELLY"]);
  const news = Party.gainXp(b.heroes, 25);  // level 1 needs 20
  assert(news.length == 3 && Party.level("kael") == 2);
  const stronger = Party.fighters()[0];
  assert(stronger.maxHp == heroById("kael").base.hp + heroById("kael").gain.hp);
  assert(stronger.hp == stronger.maxHp);  // a full hero stays full
}

export function savesRoundTrip(): void {
  Party.newGame();
  Party.gold = 123;
  Party.markDone("chest.1");
  Party.save();
  Party.newGame();
  assert(Party.gold != 123 && !Party.done("chest.1"));
  Party.load();
  assert(Party.gold == 123 && Party.done("chest.1") && Party.hasSave);
}
