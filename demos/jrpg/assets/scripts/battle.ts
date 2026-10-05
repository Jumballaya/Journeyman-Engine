// The battle scene: runs a Battle (lib/battle.ts), asks the player for each
// hero's command when their gauge fills (gauges wait meanwhile), plays every
// turn back on the Stage, and ends in rewards, escape or defeat.
// GameState "encounter": an ENCOUNTERS index, or -1 for the wyrm.
import { GameState, Input, Menu, Music, Params, Scene, Sound, UI, formatNumber } from "@jm/runtime";
import { Battle, Command, Fighter, Hit, HitKind, Outcome, Turn, ATB_FULL } from "./lib/battle";
import { ATTACK, Aim, BOSS_ENCOUNTER, ENCOUNTERS, Effect, Element, ItemDef, Skill, enemyNamed } from "./lib/data";
import { Party } from "./lib/party";
import { Stage } from "./lib/stage";

enum Phase { Intro, Running, Command, List, Target, Playing, Results, Over }

const TURN_SECONDS: f32 = 0.9;
const COMMANDS = ["cmd-fight", "cmd-tech", "cmd-item", "cmd-defend", "cmd-run"];
const LIST_ROWS = 6;

if (!Party.started) Party.newGame();  // launched directly (JM_ENTRY_SCENE)
// Param "encounter" overrides it, for testing (scenes/test_boss.scene.json).
const encounter = <i32>Params.number("encounter", GameState.getNumber("encounter"));
const bossFight = encounter < 0;
const names = bossFight ? BOSS_ENCOUNTER : ENCOUNTERS[min(encounter, ENCOUNTERS.length - 1)];
const foes = new Array<Fighter>();
for (let i = 0; i < names.length; i++) foes.push(Fighter.enemy(enemyNamed(names[i])));
const heroes = Party.fighters();
const battle = new Battle(heroes, foes, Party.inventory);
const stage = new Stage(heroes, foes, bossFight);
const music = new Music(bossFight ? "music_boss" : "music_battle");
const commands = menu(COMMANDS);
const cancel = new Sound("cancel");

let phase = Phase.Intro;
let phaseTime: f32 = 0;
let messageTime: f32 = 0;
let actor: Fighter = heroes[0];       // the hero choosing a command
let command = Command.Fight;
let skill: Skill = ATTACK;
let item: ItemDef | null = null;
let listMenu = new Menu([]);
let listSkills: Skill[] = [];
let listItems: ItemDef[] = [];
let targets: Fighter[] = [];
let targetIndex: i32 = 0;
let turn: Turn | null = null;
let landed = false;                   // the playing turn's hits have been shown
let won = false;
let fallen = false;                   // game over has been requested

music.play(0.55);
say(bossFight ? "THE CINDER WYRM RISES!" : foes.length == 1 ? foes[0].name + " APPEARS!" : "MONSTERS APPEAR!");

function menu(ids: string[]): Menu {
  return new Menu(ids).sounds(new Sound("cursor"), new Sound("confirm"), 0.5, 0.5);
}

function say(text: string, seconds: f32 = 1.4): void {
  UI.setText("message", text);
  UI.setVisible("message", true, "hidden");
  messageTime = seconds;
}

function enter(next: Phase): void {
  phase = next;
  phaseTime = 0;
  UI.setVisible("commands", next == Phase.Command, "hidden");
  UI.setVisible("list", next == Phase.List, "hidden");
  UI.setVisible("cursor", next == Phase.Target, "hidden");
}

// ---- Choosing a command -----------------------------------------------------------

function beginCommand(hero: Fighter): void {
  actor = hero;
  stage.ready(hero, true);
  UI.toggleClass("cmd-tech", "disabled", !hasUsableSkill(hero));
  UI.toggleClass("cmd-item", "disabled", Party.inventory.owned().length == 0);
  UI.toggleClass("cmd-run", "disabled", !battle.canRun);
  commands.select(0);
  enter(Phase.Command);
}

function hasUsableSkill(hero: Fighter): bool {
  for (let i = 0; i < hero.skills.length; i++) if (battle.canUse(hero, hero.skills[i])) return true;
  return false;
}

function updateCommand(): void {
  const choice = commands.update();
  if (choice == "cmd-fight") {
    command = Command.Fight;
    skill = ATTACK;
    chooseTarget(Aim.Enemy);
  } else if (choice == "cmd-tech") {
    if (hasUsableSkill(actor)) openList(true);
    else cancel.play(0.5);
  } else if (choice == "cmd-item") {
    if (Party.inventory.owned().length > 0) openList(false);
    else cancel.play(0.5);
  } else if (choice == "cmd-defend") {
    command = Command.Defend;
    perform(null);
  } else if (choice == "cmd-run") {
    if (!battle.canRun) cancel.play(0.5);
    else {
      command = Command.Run;
      perform(null);
    }
  }
}

// The hero's techs (true) or the party's items, one per row.
function openList(techs: bool): void {
  listSkills = techs ? actor.skills : [];
  listItems = techs ? [] : Party.inventory.owned();
  const count = techs ? listSkills.length : listItems.length;
  const ids = new Array<string>();
  for (let i = 0; i < LIST_ROWS; i++) {
    const id = "row-" + i.toString();
    UI.setVisible(id, i < count, "hidden");
    if (i >= count) continue;
    ids.push(id);
    if (techs) {
      const s = listSkills[i];
      UI.setText(id + "-name", s.name);
      UI.setText(id + "-cost", s.mp.toString() + (s.partner.length > 0 ? "+" : ""));
      UI.toggleClass(id, "disabled", !battle.canUse(actor, s));
    } else {
      UI.setText(id + "-name", listItems[i].name);
      UI.setText(id + "-cost", "x" + Party.inventory.count(listItems[i].name).toString());
      UI.toggleClass(id, "disabled", false);
    }
  }
  listMenu = menu(ids);
  listMenu.select(0);
  command = techs ? Command.Skill : Command.Item;
  enter(Phase.List);
}

function updateList(): void {
  if (Input.pressed("back")) {
    cancel.play(0.5);
    enter(Phase.Command);
    return;
  }
  if (listMenu.update().length == 0) return;
  const i = listMenu.index;
  if (command == Command.Skill) {
    if (!battle.canUse(actor, listSkills[i])) {
      cancel.play(0.5);
      return;
    }
    skill = listSkills[i];
    chooseTarget(skill.aim);
  } else {
    item = listItems[i];
    chooseTarget(item!.aim);
  }
}

// Whole-side moves go at once; others pick from the valid targets.
function chooseTarget(aim: Aim): void {
  if (aim == Aim.Enemies || aim == Aim.Allies) {
    perform(null);
    return;
  }
  targets = battle.targets(actor, aim);
  if (targets.length == 0) {
    cancel.play(0.5);
    return;
  }
  targetIndex = 0;
  enter(Phase.Target);
  placeCursor();
}

function placeCursor(): void {
  const t = targets[targetIndex];
  UI.setStyle("cursor", "left", (<i32>stage.screenX(t) - 20).toString() + "px");
  UI.setStyle("cursor", "top", (<i32>stage.screenY(t) - 4).toString() + "px");
}

function updateTarget(): void {
  if (Input.pressed("back")) {
    cancel.play(0.5);
    enter(command == Command.Fight ? Phase.Command : Phase.List);
    return;
  }
  const step = (Input.pressed("down") || Input.pressed("right") ? 1 : 0) - (Input.pressed("up") || Input.pressed("left") ? 1 : 0);
  if (step != 0) {
    targetIndex = (targetIndex + step + targets.length) % targets.length;
    new Sound("cursor").play(0.5);
    placeCursor();
  }
  if (Input.pressed("confirm")) perform(targets[targetIndex]);
}

function perform(target: Fighter | null): void {
  stage.ready(actor, false);
  play(battle.act(actor, command, skill, item, target));
}

// ---- Playing a turn back -----------------------------------------------------------

function play(t: Turn): void {
  turn = t;
  landed = false;
  if (t.name != "ATTACK" && t.name != "DEFEND") say(t.name, 1.0);
  const casting = t.skill !== null && t.skill!.magic;
  stage.lunge(t.actor, true, casting);
  if (t.partner !== null) stage.lunge(t.partner!, true);
  enter(Phase.Playing);
}

function effectOf(s: Skill | null): string {
  if (s === null) return "heal";  // items
  if (s.effect == Effect.Heal || s.effect == Effect.Revive || s.effect == Effect.Cure) return "heal";
  if (s.element == Element.Fire) return "fire";
  if (s.element == Element.Ice) return "ice";
  if (s.element == Element.Bolt) return "bolt";
  return s.effect == Effect.Sleep ? "heal" : "slash";
}

function soundOf(s: Skill | null, hit: Hit): string {
  if (hit.kind == HitKind.Miss) return "miss";
  if (hit.kind == HitKind.Heal || hit.kind == HitKind.Revive || hit.kind == HitKind.Mana) return "heal";
  if (hit.kind == HitKind.Status) return "status";
  if (hit.critical) return "critical";
  const e = effectOf(s);
  return e == "slash" || e == "heal" ? "hit" : e;
}

function land(t: Turn): void {
  let lastSound = "";
  for (let i = 0; i < t.hits.length; i++) {
    const hit = t.hits[i];
    if (hit.kind != HitKind.Miss && hit.label != "POISON") stage.effect(hit.target, effectOf(t.skill));
    if (hit.kind == HitKind.Damage) {
      const poison = hit.label == "POISON";
      stage.show(hit.target, hit.amount.toString() + (hit.critical ? "!" : ""), poison ? "#c070ff" : hit.critical ? "#ffd860" : "#ffffff", true);
    } else if (hit.kind == HitKind.Heal || hit.kind == HitKind.Revive) {
      stage.show(hit.target, hit.amount.toString(), "#80ff90", false);
    } else if (hit.kind == HitKind.Mana) {
      stage.show(hit.target, hit.amount.toString(), "#80c0ff", false);
    } else {
      stage.show(hit.target, hit.label, hit.kind == HitKind.Miss ? "#a8b4dc" : "#ffd860", hit.label != "CURED" && hit.kind != HitKind.Miss);
    }
    if (!hit.target.alive && !hit.target.hero) new Sound("defeat").play(0.6);
    const sound = soundOf(t.skill, hit);
    if (sound != lastSound) new Sound(sound).play(0.6);
    lastSound = sound;
  }
  if (t.name == "RUN" && battle.outcome == Outcome.Fled) say("ESCAPED!");
}

function updatePlaying(): void {
  const t = turn!;
  if (!landed && phaseTime >= 0.25) {
    landed = true;
    land(t);
  }
  if (phaseTime >= 0.6) {
    stage.lunge(t.actor, false);
    if (t.partner !== null) stage.lunge(t.partner!, false);
  }
  if (phaseTime < TURN_SECONDS) return;
  if (battle.outcome == Outcome.Won) win();
  else if (battle.outcome == Outcome.Lost) lose();
  else if (battle.outcome == Outcome.Fled) leave("map");
  else enter(Phase.Running);
}

// ---- Endings -------------------------------------------------------------------------

function win(): void {
  won = true;
  music.stop();
  new Sound("victory").play(0.6);
  Party.keep(heroes);
  const levels = Party.gainXp(heroes, battle.xp);
  Party.gold += battle.gold;
  const lines = ["VICTORY!", "GAINED " + battle.xp.toString() + " XP AND " + battle.gold.toString() + " GOLD."];
  for (let i = 0; i < levels.length && lines.length < 6; i++) lines.push(levels[i]);
  if (levels.length > 0) new Sound("level_up").play(0.6);
  for (let i = 0; i < 6; i++) UI.setText("res-" + i.toString(), i < lines.length ? lines[i] : "");
  UI.setVisible("results", true, "hidden");
  if (bossFight) Party.markDone("wyrm");
  enter(Phase.Results);
}

function lose(): void {
  music.fadeOut(1.0);
  say("THE PARTY HAS FALLEN...", 9);
  enter(Phase.Over);
}

function leave(scene: string): void {
  if (!won) Party.keep(heroes);
  music.fadeOut(0.4);
  enter(Phase.Over);
  Scene.transition(scene, 0.6);
}

// ---- Every frame -------------------------------------------------------------------

function updateParty(): void {
  for (let i = 0; i < heroes.length; i++) {
    const h = heroes[i];
    UI.setText("p-hp-" + h.id, h.hp.toString() + "/" + h.maxHp.toString());
    UI.setText("p-mp-" + h.id, h.mp.toString());
    UI.setStyle("p-atb-" + h.id, "width", formatNumber(<f64>(h.atb / ATB_FULL * 100), 1) + "%");
    UI.toggleClass("p-atb-" + h.id, "full", h.atb >= ATB_FULL);
    UI.toggleClass("p-name-" + h.id, "ready", phase != Phase.Running && h === actor && phase != Phase.Playing);
    UI.toggleClass("p-name-" + h.id, "down", !h.alive);
  }
}

export function onUpdate(dt: f32): void {
  phaseTime += dt;
  messageTime -= dt;
  if (messageTime <= 0) UI.setVisible("message", false, "hidden");
  stage.update(dt);
  updateParty();

  if (phase == Phase.Intro) {
    if (phaseTime > 1.2) enter(Phase.Running);
  } else if (phase == Phase.Running) {
    battle.tick(dt);
    const enemyTurn = battle.enemyTurn();
    if (enemyTurn !== null) play(enemyTurn);
    else {
      const hero = battle.readyHero();
      if (hero !== null) beginCommand(hero);
    }
  } else if (phase == Phase.Command) updateCommand();
  else if (phase == Phase.List) updateList();
  else if (phase == Phase.Target) updateTarget();
  else if (phase == Phase.Playing) updatePlaying();
  else if (phase == Phase.Results) {
    if (phaseTime > 1.0 && Input.pressed("confirm")) leave(bossFight ? "ending" : "map");
  } else if (phase == Phase.Over && battle.outcome == Outcome.Lost && phaseTime > 3 && !fallen) {
    fallen = true;
    Scene.transition("game_over", 1.0);
  }
}
