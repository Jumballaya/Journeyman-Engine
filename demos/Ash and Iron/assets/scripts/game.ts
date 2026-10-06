// The game, run from the HUD entity of every map scene: walking the grid,
// talking, menus, the shop, and turn-based fights. People, enemies, caches,
// pickups and exits are authored in the scene (see the prefabs); what they
// say, carry and hit for lives in the data tables.
//
// Params: "place" (the location's name) and "music".
import {
  Camera, Entity, GameState, Input, Music, Overrides, Params, Random, Scene, Sound, UI, World, spawn,
} from "@jm/runtime";
import {
  ABILITIES, ATLAS, AbilityDef, CHOICES, Choice, EnemyDef, OPENINGS, QUESTS, STOCK,
  enemy, item, line, nextLevel, person, quest, stage,
} from "./lib/db";
import { Cell, Grid, LIFT, dist } from "./lib/grid";
import * as hero from "./lib/state";
import * as slots from "./lib/slots";
import { Pointer } from "./lib/pointer";

enum Mode { Explore, Talk, Menu, Shop, Combat, Over, Leaving }

const PLACE = Params.text("place", "Somewhere");
const SCENE = sceneName();
const grid = new Grid();
const me = World.find("player");
let mode = Mode.Explore;

const sounds = new Map<string, Sound>();
function play(name: string, gain: f32 = 0.7): void {
  if (name.length == 0) return;
  if (!sounds.has(name)) sounds.set(name, new Sound(name));
  sounds.get(name).play(gain);
}
const music = new Music(Params.text("music", "music_town"));
let fightMusic: Music | null = null;

function sceneName(): string {
  const path = Scene.current;
  const slash = path.lastIndexOf("/");
  const name = path.substring(slash + 1);
  const dot = name.indexOf(".");
  return dot < 0 ? name : name.substring(0, dot);
}

// ---- figures: the hero, people and enemies drawn from atlas frames ---------------

enum Facing { Down, Up, Left, Right }

class Figure {
  entity: Entity;
  base: string = "";  // "raider" for raider_down_0.., "drone" for drone_0..
  walker: bool;       // has _down/_up/_side frames
  frames: i32;
  facing: Facing = Facing.Down;
  cell: Cell = new Cell(0, 0);
  // A step in progress: from the world point to the cell.
  fromX: f32 = 0; fromY: f32 = 0;
  moving: f32 = -1;  // seconds into the step, -1 when still
  clock: f32 = 0;

  constructor(entity: Entity, sprite: string) {
    this.entity = entity;
    this.walker = sprite.endsWith("_down_0");
    this.base = this.walker ? sprite.substring(0, sprite.length - 7) : sprite.substring(0, sprite.lastIndexOf("_"));
    this.frames = this.walker ? 3 : (this.base == "warden" ? 3 : 2);
    this.cell = grid.cellOf(entity);
    grid.placeAt(entity, this.cell);
    this.show(0);
  }

  show(frame: i32): void {
    if (this.walker) {
      const dir = this.facing == Facing.Down ? "down" : this.facing == Facing.Up ? "up" : "side";
      this.entity.sprite.setTexture(ATLAS + this.base + "_" + dir + "_" + frame.toString());
      // The side frames face left.
      this.entity.transform.scaleX = this.facing == Facing.Right ? -Mathf.abs(this.entity.transform.scaleX) : Mathf.abs(this.entity.transform.scaleX);
    } else {
      this.entity.sprite.setTexture(ATLAS + this.base + "_" + (frame % this.frames).toString());
    }
  }

  face(c: Cell): void {
    const dx = c.x - this.cell.x, dy = c.y - this.cell.y;
    if (abs(dx) >= abs(dy) && dx != 0) this.facing = dx > 0 ? Facing.Right : Facing.Left;
    else if (dy != 0) this.facing = dy > 0 ? Facing.Up : Facing.Down;
    this.show(0);
  }

  stepTo(c: Cell): void {
    this.face(c);
    this.fromX = grid.worldX(this.cell.x);
    this.fromY = grid.worldY(this.cell.y);
    this.cell = c;
    this.moving = 0;
  }

  // Advances the step; true while moving.
  update(dt: f32, seconds: f32 = 0.16): bool {
    this.clock += dt;
    if (this.moving < 0) {
      if (!this.walker) this.show(<i32>(this.clock * 4));  // machines idle-animate
      return false;
    }
    this.moving += dt;
    const t = Mathf.min(1, this.moving / seconds);
    grid.place(this.entity, this.fromX + (grid.worldX(this.cell.x) - this.fromX) * t, this.fromY + (grid.worldY(this.cell.y) - this.fromY) * t);
    const cycle = [1, 0, 2, 0];
    this.show(this.walker ? cycle[<i32>(this.moving / seconds * 2) % 4] : <i32>(this.clock * 6));
    if (t >= 1) { this.moving = -1; this.show(0); return false; }
    return true;
  }
}

const player = new Figure(me, "player_down_0");

// ---- the HUD -------------------------------------------------------------------

const LOG: string[] = [];
let toastTime: f32 = 0;

function say(text: string): void {
  LOG.push(text);
  if (LOG.length > 4) LOG.shift();
  for (let i = 0; i < 4; i++) UI.setText("log-" + (i + 1).toString(), i < LOG.length ? LOG[LOG.length - 1 - i] : "");
  UI.setText("toast", text);
  UI.setVisible("toast", true, "hidden");
  toastTime = 2.5;
}

// A passing word in the toast, kept out of the log (what hovering tells you).
function hint(text: string): void {
  UI.setText("toast", text);
  UI.setVisible("toast", true, "hidden");
  toastTime = 1.2;
}

function drainNotes(): void {
  while (hero.NOTES.length > 0) {
    const n = hero.NOTES.shift();
    say(n.text);
    play(n.sound);
  }
}

function bar(id: string, fraction: f32): void { UI.setStyle(id, "width", (Mathf.round(Mathf.max(0, Mathf.min(1, fraction)) * 100)).toString() + "%"); }

let shownAp = -1;
function refreshHud(ap: i32 = -1): void {
  UI.setText("hp-text", hero.hp().toString() + "/" + hero.maxHp().toString());
  bar("hp-fill", <f32>hero.hp() / <f32>hero.maxHp());
  UI.setText("lvl-text", "LV " + hero.level().toString());
  UI.setText("scrip-text", hero.scrip().toString());
  bar("xp-fill", <f32>hero.xp() / <f32>max(1, nextXp()));
  const q = hero.trackedQuest();
  UI.setVisible("tracker", q.length > 0, "hidden");
  if (q.length > 0) {
    const def = quest(q);
    const s = stage(q, hero.step(q));
    UI.setText("quest-name", def != null ? def.name : q);
    UI.setText("quest-step", s != null ? s.text : "");
  }
  // Abilities 1-4, dimmed until known.
  for (let i = 0; i < 4 && i < ABILITIES.length; i++) {
    const a = ABILITIES[i];
    const slot = "ab-" + (i + 1).toString();
    UI.setAttribute(slot + "-icon", "src", ATLAS + a.icon);
    UI.toggleClass(slot, "locked", !hero.knows(a.id));
    UI.toggleClass(slot, "unusable", mode == Mode.Combat && hero.cantUse(a.id, ap) != "");
    UI.toggleClass(slot, "armed", mode == Mode.Combat && armedAbility().id == a.id);
  }
  // Action points, in a fight.
  UI.setVisible("ap-row", ap >= 0, "hidden");
  if (ap != shownAp) {
    shownAp = ap;
    for (let i = 1; i <= 10; i++) {
      UI.setVisible("ap-" + i.toString(), i <= hero.maxAp(), "hidden");
      UI.toggleClass("ap-" + i.toString(), "spent", i > ap);
    }
  }
}

// Experience the next level needs (the current total at the top level).
function nextXp(): i32 {
  const n = nextLevel(hero.level());
  return n != null ? n.xp : hero.xp();
}

function showPanel(id: string): void {
  const panels = ["dlg", "menu", "shop", "over", "target"];
  for (let i = 0; i < panels.length; i++) UI.setVisible(panels[i], panels[i] == id, "hidden");
}

// ---- what scene entities are: their params, with the defaults their scripts declare
// (person.ts, foe.ts, pickup.ts...): an entry left at a default stores nothing.

function whoOf(e: Entity): string { return e.params.text("who", e.hasTag("npc") ? "villager" : "raider"); }
function itemOf(p: Entity): string { return p.params.text("item", "tonic"); }
function keyOf(e: Entity): string {
  const key = e.params.text("key");
  return key.length > 0 ? key : e.hasTag("pickup") ? itemOf(e) : whoOf(e);
}

// ---- arriving -------------------------------------------------------------------

function arrive(): void {
  if (!GameState.has("hero.xp")) hero.newGame();  // played straight from the editor
  const at = GameState.getString("arrive");
  const spot = at.length > 0 ? World.find(at) : Entity.NONE;
  if (!spot.isNone) {
    player.cell = grid.cellOf(spot);
    player.cell.y = grid.map.tileY(spot.transform.y);
  } else if (GameState.getString("pos.scene") == SCENE) {
    player.cell = new Cell(<i32>GameState.getNumber("pos.x"), <i32>GameState.getNumber("pos.y"));
  }
  GameState.remove("arrive");
  grid.placeAt(me, player.cell);
  me.addTag("figure");
  hero.setFlag("visited_" + SCENE);
  openGates();
  music.play(0.5);
  UI.setText("place", PLACE);
  UI.setVisible("place", true, "hidden");
  toastTime = 0;
  showPanel("");
  refreshHud();
}

// Exits whose condition holds stand open: a closed gate tile under one opens.
function openGates(): void {
  const exits = World.findAll("exit");
  for (let i = 0; i < exits.length; i++) {
    const c = grid.cellOf(exits[i]);
    c.y = grid.map.tileY(exits[i].transform.y);
    if (hero.test(exits[i].params.text("when")) && grid.map.at(c.x, c.y) == "G") grid.map.set(c.x, c.y, "g");
  }
}

function exitAt(c: Cell): Entity {
  const exits = World.findAll("exit");
  for (let i = 0; i < exits.length; i++) {
    if (grid.map.tileX(exits[i].transform.x) == c.x && grid.map.tileY(exits[i].transform.y) == c.y) return exits[i];
  }
  return Entity.NONE;
}

function pickupAt(c: Cell): Entity {
  const all = World.findAll("pickup");
  for (let i = 0; i < all.length; i++) {
    if (grid.map.tileX(all[i].transform.x) == c.x && grid.map.tileY(all[i].transform.y) == c.y) return all[i];
  }
  return Entity.NONE;
}

function leave(exit: Entity): void {
  if (!hero.test(exit.params.text("when"))) {
    say(exit.params.text("locked", "It won't open."));
    play("ui_back");
    return;
  }
  mode = Mode.Leaving;
  GameState.setString("arrive", exit.params.text("at"));
  GameState.remove("pos.scene");
  play("door");
  music.fadeOut(0.5);
  Scene.transition(exit.params.text("to"), 0.6);
}

// ---- exploring ------------------------------------------------------------------

function direction(): Cell | null {
  if (Input.down("left")) return new Cell(-1, 0);
  if (Input.down("right")) return new Cell(1, 0);
  if (Input.down("up")) return new Cell(0, 1);
  if (Input.down("down")) return new Cell(0, -1);
  return null;
}

function ahead(): Cell {
  const f = player.facing;
  return new Cell(player.cell.x + (f == Facing.Right ? 1 : f == Facing.Left ? -1 : 0), player.cell.y + (f == Facing.Up ? 1 : f == Facing.Down ? -1 : 0));
}

// Tries a step; walking into a closed exit or a person says why it can't.
function tryStep(d: Cell): bool {
  const to = new Cell(player.cell.x + d.x, player.cell.y + d.y);
  if (grid.walkable(to.x, to.y)) {
    player.stepTo(to);
    play("step", 0.25);
    return true;
  }
  player.face(to);
  const exit = exitAt(to);
  if (!exit.isNone && grid.solid(to.x, to.y)) leave(exit);
  return false;
}

function arrived(): void {
  GameState.setNumber("pos.x", player.cell.x);
  GameState.setNumber("pos.y", player.cell.y);
  GameState.setString("pos.scene", SCENE);
  const p = pickupAt(player.cell);
  if (!p.isNone) collect(p);
  const exit = exitAt(player.cell);
  if (!exit.isNone) { leave(exit); return; }
  // Slag burns: a step onto it costs health (and in a fight, it's worth walking around).
  if (grid.map.is(player.cell.x, player.cell.y, "hazard")) {
    play("ember", 0.5);
    float(me, "2", 1, 0.5, 0.1);
    hero.setHp(hero.hp() - 2);
    say("The slag burns!");
    refreshHud(inFight ? ap : -1);
    if (hero.hp() <= 0) { die(); return; }
  }
  if (mode == Mode.Explore) checkAmbush();
}

function collect(p: Entity): void {
  hero.give(itemOf(p), <i32>p.params.number("count", 1));
  GameState.setNumber("gone." + keyOf(p), 1);
  p.destroy();
}

function interact(): void {
  const c = ahead();
  useThing(grid.occupant(c.x, c.y));
}

function useThing(who: Entity): void {
  if (who.isNone) return;
  if (who.hasTag("npc")) talk(who);
  else if (who.hasTag("cache")) search(who);
  else if (who.hasTag("enemy")) startCombat(who);
}

function search(cache: Entity): void {
  const key = "gone." + keyOf(cache);
  if (GameState.getNumber(key) > 0) { say("Nothing left."); return; }
  GameState.setNumber(key, 1);
  play("door");
  const items = cache.params.text("items").split(",");
  for (let i = 0; i < items.length; i++) if (items[i].trim().length > 0) hero.give(items[i].trim());
  cache.sprite.setColor(0.6, 0.6, 0.6);
}

// Clicked somewhere: walk there; clicked someone or something: walk up to it, then use it.
let route: Cell[] = [];
let routeTarget = Entity.NONE;

// The tile under the pointer (null over the HUD, whose panels take clicks of their own).
function pointerCell(): Cell | null {
  if (!Pointer.inside || Pointer.over("status") || Pointer.over("tracker") || Pointer.over("abilities") || Pointer.over("right")) return null;
  const c = new Cell(grid.map.tileX(Pointer.x), grid.map.tileY(Pointer.y));
  return grid.inside(c.x, c.y) ? c : null;
}

function planWalk(c: Cell): void {
  route = [];
  routeTarget = grid.occupant(c.x, c.y);
  if (routeTarget.equals(me)) routeTarget = Entity.NONE;
  if (routeTarget.isNone && grid.solid(c.x, c.y)) {
    // A closed gate: walk up to it and push.
    if (exitAt(c).isNone) return;
    routeTarget = exitAt(c);
  }
  route = grid.pathToward(player.cell, c, routeTarget.isNone ? 0 : 1);
  if (route.length == 0 && routeTarget.isNone && !c.equals(player.cell)) say("Can't get there.");
}

// One step along the route when standing still; the target's turn at the end.
function followRoute(): bool {
  if (route.length > 0) {
    const next = route.shift();
    if (!grid.walkable(next.x, next.y)) { route = []; return false; }
    player.stepTo(next);
    play("step", 0.25);
    wasMoving = true;
    return true;
  }
  if (routeTarget.isNone) return false;
  const target = routeTarget;
  routeTarget = Entity.NONE;
  if (!target.isAlive) return false;
  const c = target.hasTag("exit") ? new Cell(grid.map.tileX(target.transform.x), grid.map.tileY(target.transform.y)) : grid.cellOf(target);
  if (abs(c.x - player.cell.x) + abs(c.y - player.cell.y) > 1) return false;
  player.face(c);
  if (target.hasTag("exit")) leave(target);
  else useThing(target);
  return true;
}

function explore(dt: f32): void {
  if (player.update(dt)) return;
  if (player.moving < 0 && wasMoving) { wasMoving = false; arrived(); if (mode != Mode.Explore) return; }
  if (Pointer.clicked) {
    const c = pointerCell();
    if (c != null) planWalk(c as Cell);
  }
  if (Input.pressed("confirm")) { route = []; interact(); return; }
  if (Input.pressed("inventory")) { openMenu(0); return; }
  if (Input.pressed("quests")) { openMenu(1); return; }
  if (Input.pressed("character")) { openMenu(2); return; }
  if (Input.pressed("back")) { openMenu(3); return; }
  const d = direction();
  if (d != null) {
    route = [];
    routeTarget = Entity.NONE;
    if (tryStep(d)) wasMoving = true;
    return;
  }
  followRoute();
}
let wasMoving = false;

// ---- talking --------------------------------------------------------------------

let talker: Figure | null = null;
let lineId = "";
let shownChars: f32 = 0;
let lineText = "";
let options: Choice[] = [];
let choice = 0;
let afterTalk = "";
let choicesAge: f32 = 0;  // seconds the current choices have been showing

function talk(npc: Entity): void {
  const id = whoOf(npc);
  talker = figureOf(npc);
  if (talker != null) (talker as Figure).face(player.cell);
  for (let i = 0; i < OPENINGS.length; i++) {
    if (OPENINGS[i].npc == id && hero.test(OPENINGS[i].when)) {
      mode = Mode.Talk;
      afterTalk = "";
      showPanel("dlg");
      showLine(OPENINGS[i].line);
      return;
    }
  }
}

function showLine(id: string): void {
  const l = line(id);
  if (l == null) { endTalk(); return; }
  lineId = id;
  const p = person(l.speaker);
  UI.setText("dlg-name", p != null ? p.name : l.speaker);
  if (p != null) UI.setAttribute("dlg-portrait", "src", ATLAS + p.portrait);
  lineText = l.text;
  shownChars = 0;
  UI.setText("dlg-text", "");
  const open = hero.run(l.action);
  if (open.length > 0) afterTalk = open;
  options = [];
  for (let i = 0; i < CHOICES.length; i++) if (CHOICES[i].line == id && hero.test(CHOICES[i].when)) options.push(CHOICES[i]);
  choice = 0;
  showChoices(false);
}

function showChoices(visible: bool): void {
  for (let i = 0; i < 4; i++) {
    const slot = "dlg-choice-" + (i + 1).toString();
    UI.setVisible(slot, visible && i < options.length, "hidden");
    if (i < options.length) {
      UI.setText(slot, options[i].text);
      UI.toggleClass(slot, "selected", i == choice);
    }
  }
  UI.setVisible("dlg-more", !visible || options.length == 0, "hidden");
}

function talking(dt: f32): void {
  if (shownChars < <f32>lineText.length) {
    const before = <i32>shownChars;
    shownChars += dt * 60;
    if (<i32>shownChars / 3 != before / 3) play("text", 0.15);
    UI.setText("dlg-text", lineText.substring(0, <i32>shownChars));
    if (Input.pressed("confirm") || Pointer.clicked) shownChars = <f32>lineText.length;
    if (shownChars >= <f32>lineText.length) { UI.setText("dlg-text", lineText); showChoices(true); choicesAge = 0; }
    return;
  }
  if (options.length > 0) {
    // Choices take clicks only once they've been up a moment: a click meant for the text mustn't pick one.
    choicesAge += dt;
    const settled = choicesAge > 0.35;
    if (Input.repeated("up", 0.3, 0.12)) { choice = (choice + options.length - 1) % options.length; play("ui_move", 0.4); showChoices(true); }
    if (Input.repeated("down", 0.3, 0.12)) { choice = (choice + 1) % options.length; play("ui_move", 0.4); showChoices(true); }
    const hovered = Pointer.overRow("dlg-choice-", options.length);
    if (Pointer.moved && hovered >= 0 && hovered != choice) { choice = hovered; play("ui_move", 0.3); showChoices(true); }
    if (Input.pressed("confirm") || (settled && Pointer.clicked && hovered >= 0)) {
      const c = options[choice];
      play("ui_select", 0.5);
      const open = hero.run(c.action);
      if (open.length > 0) afterTalk = open;
      if (c.goto.length > 0) showLine(c.goto); else endTalk();
    }
    return;
  }
  const onward = Input.pressed("confirm") || Pointer.clicked;
  if (onward || Input.pressed("back") || Pointer.rightClicked) {
    const l = line(lineId);
    if (l != null && l.next.length > 0 && onward) showLine(l.next); else endTalk();
  }
}

function endTalk(): void {
  showPanel("");
  mode = Mode.Explore;
  talker = null;
  refreshHud();
  if (hero.flag("victory")) { finish(); return; }
  if (afterTalk == "shop") openShop();
}

function finish(): void {
  mode = Mode.Leaving;
  music.fadeOut(1.5);
  Scene.transition("ending", 1.5);
}

// ---- the menu: inventory, quests, character, system ------------------------------

const TABS = ["inv", "quests", "char", "sys"];
let tab = 0;
let row = 0;

function openMenu(t: i32): void {
  mode = Mode.Menu;
  tab = t;
  row = 0;
  play("ui_select", 0.5);
  showPanel("menu");
  drawMenu();
}

function closeMenu(): void {
  showPanel("");
  play("ui_back", 0.5);
  mode = inFight ? Mode.Combat : Mode.Explore;
  refreshHud(inFight ? ap : -1);
}

function rows(): i32 {
  if (tab == 0) return min(8, hero.carried().length + (hero.equipped("weapon").length > 0 ? 1 : 0) + (hero.equipped("armor").length > 0 ? 1 : 0));
  if (tab == 3) return 5;
  return 0;
}

// Inventory rows: equipped things first, then the pack.
function inventory(): string[] {
  const out: string[] = [];
  if (hero.equipped("weapon").length > 0) out.push(hero.equipped("weapon"));
  if (hero.equipped("armor").length > 0) out.push(hero.equipped("armor"));
  const pack = hero.carried();
  for (let i = 0; i < pack.length; i++) out.push(pack[i]);
  return out;
}

function drawMenu(): void {
  for (let i = 0; i < TABS.length; i++) {
    UI.toggleClass("tab-" + TABS[i], "selected", i == tab);
    UI.setVisible("page-" + TABS[i], i == tab, "hidden");
  }
  if (tab == 0) {
    const list = inventory();
    row = min(row, max(0, list.length - 1));
    UI.setVisible("inv-empty", list.length == 0, "hidden");
    for (let i = 0; i < 8; i++) {
      const id = "inv-" + (i + 1).toString();
      UI.setVisible(id, i < list.length, "hidden");
      if (i >= list.length) continue;
      const def = item(list[i]);
      if (def == null) continue;
      const worn = hero.equipped(def.kind) == def.id;
      UI.setAttribute(id + "-icon", "src", ATLAS + def.icon);
      UI.setText(id + "-name", def.name + (worn ? "  (E)" : ""));
      UI.setText(id + "-count", worn ? "" : "x" + hero.count(def.id).toString());
      UI.toggleClass(id, "selected", i == row);
    }
    if (list.length > 0) {
      const def = item(list[row]);
      if (def != null) {
        UI.setAttribute("inv-d-icon", "src", ATLAS + def.icon);
        UI.setText("inv-d-name", def.name);
        UI.setText("inv-d-desc", def.description);
        const action = def.kind == "consumable" ? "Use" : def.kind == "weapon" || def.kind == "armor" ? (hero.equipped(def.kind) == def.id ? "Unequip" : "Equip") : "";
        UI.setText("inv-d-hint", action.length > 0 ? "E  " + action + (inFight && def.kind == "consumable" ? " (2 AP)" : "") : "");
      }
    }
    UI.setVisible("inv-detail", list.length > 0, "hidden");
  } else if (tab == 1) {
    let n = 0;
    for (let i = 0; i < QUESTS.length && n < 3; i++) {
      const q = QUESTS[i];
      const s = hero.step(q.id);
      if (s == 0) continue;
      n++;
      const id = "q-" + n.toString();
      UI.setVisible(id, true, "hidden");
      UI.setText(id + "-name", q.name);
      const st = stage(q.id, s);
      UI.setText(id + "-text", s == hero.DONE ? "Done." : st != null ? st.text : q.description);
      UI.toggleClass(id, "done", s == hero.DONE);
    }
    for (let i = n + 1; i <= 3; i++) UI.setVisible("q-" + i.toString(), false, "hidden");
    UI.setVisible("q-empty", n == 0, "hidden");
  } else if (tab == 2) {
    UI.setText("char-level", "Level " + hero.level().toString());
    UI.setText("char-hp", hero.hp().toString() + " / " + hero.maxHp().toString());
    UI.setText("char-ap", hero.maxAp().toString());
    UI.setText("char-xp", hero.xp().toString() + " / " + nextXp().toString());
    UI.setText("char-scrip", hero.scrip().toString());
    UI.setText("char-weapon", hero.equipped("weapon").length > 0 ? hero.itemName(hero.equipped("weapon")) : "Fists");
    UI.setText("char-armor", hero.equipped("armor").length > 0 ? hero.itemName(hero.equipped("armor")) : "Rags");
    for (let i = 0; i < 4 && i < ABILITIES.length; i++) {
      const a = ABILITIES[i];
      const id = "cab-" + (i + 1).toString();
      UI.setAttribute(id + "-icon", "src", ATLAS + a.icon);
      UI.setText(id + "-name", hero.knows(a.id) ? a.name + "  " + a.ap.toString() + " AP" : "???");
      UI.setText(id + "-info", hero.knows(a.id) ? a.description : "Not yet learned.");
      UI.toggleClass(id, "locked", !hero.knows(a.id));
    }
  } else {
    for (let s = 1; s <= 3; s++) {
      const sum = slots.summary(s);
      UI.setText("sys-" + s.toString(), "Save to slot " + s.toString() + ":  " + (sum.used ? sum.place + "  LV " + sum.level.toString() + "  " + slots.clock(sum.seconds) : "empty"));
      UI.toggleClass("sys-" + s.toString(), "disabled", inFight);
    }
    UI.setText("sys-4", "Load last save");
    UI.setText("sys-5", "Quit to title");
    for (let i = 1; i <= 5; i++) UI.toggleClass("sys-" + i.toString(), "selected", i - 1 == row);
  }
}

function menu(): void {
  if (Input.pressed("back") || Pointer.rightClicked || (Pointer.clicked && !Pointer.over("menu"))) { closeMenu(); return; }
  if (Pointer.clicked) {
    for (let i = 0; i < TABS.length; i++) {
      if (Pointer.over("tab-" + TABS[i]) && i != tab) { tab = i; row = 0; play("ui_move", 0.4); drawMenu(); return; }
    }
  }
  const rowPrefix = tab == 0 ? "inv-" : tab == 3 ? "sys-" : "";
  const hovered = rowPrefix.length > 0 ? Pointer.overRow(rowPrefix, rows()) : -1;
  if (Pointer.moved && hovered >= 0 && hovered != row) { row = hovered; play("ui_move", 0.3); drawMenu(); }
  if (Pointer.clicked && hovered >= 0) {
    row = hovered;
    if (tab == 0) useItem(); else system();
    return;
  }
  const hotkeys = ["inventory", "quests", "character"];
  for (let i = 0; i < 3; i++) {
    if (Input.pressed(hotkeys[i])) {
      if (tab == i) { closeMenu(); return; }
      tab = i; row = 0; play("ui_move", 0.4); drawMenu();
    }
  }
  if (Input.repeated("left", 0.3, 0.15)) { tab = (tab + 3) % 4; row = 0; play("ui_move", 0.4); drawMenu(); }
  if (Input.repeated("right", 0.3, 0.15)) { tab = (tab + 1) % 4; row = 0; play("ui_move", 0.4); drawMenu(); }
  const n = rows();
  if (n > 0 && Input.repeated("up", 0.3, 0.1)) { row = (row + n - 1) % n; play("ui_move", 0.4); drawMenu(); }
  if (n > 0 && Input.repeated("down", 0.3, 0.1)) { row = (row + 1) % n; play("ui_move", 0.4); drawMenu(); }
  if (!Input.pressed("confirm")) return;
  if (tab == 0) useItem();
  else if (tab == 3) system();
}

function useItem(): void {
  const list = inventory();
  if (row >= list.length) return;
  const def = item(list[row]);
  if (def == null) return;
  if (def.kind == "weapon" || def.kind == "armor") {
    if (inFight && ap < 2) { say("Not enough AP"); return; }
    hero.equip(def.id);
    if (inFight) spend(2);
  } else if (def.kind == "consumable") {
    if (hero.hp() >= hero.maxHp()) { say("Already at full health"); return; }
    if (inFight && ap < 2) { say("Not enough AP"); return; }
    hero.take(def.id);
    hero.heal(def.power);
    play("mend");
    float(me, "+" + def.power.toString(), 0.4, 1, 0.5);
    if (inFight) spend(2);
  } else {
    say(def.description);
    return;
  }
  drainNotes();
  refreshHud(inFight ? ap : -1);
  drawMenu();
}

function system(): void {
  if (row < 3) {
    if (inFight) { say("Not while fighting."); play("ui_back"); return; }
    slots.save(row + 1, PLACE, hero.level());
    play("save");
    say("Saved to slot " + (row + 1).toString());
    drawMenu();
  } else if (row == 3) {
    const last = slots.latest();
    if (last == 0) { say("No save yet."); return; }
    loadSlot(last);
  } else {
    mode = Mode.Leaving;
    music.fadeOut(0.8);
    Scene.transition("title", 0.8);
  }
}

function loadSlot(slot: i32): void {
  if (!slots.load(slot)) return;
  mode = Mode.Leaving;
  music.fadeOut(0.6);
  Scene.transition(GameState.getString("pos.scene", "cinderwell"), 0.6);
}

// ---- the shop -------------------------------------------------------------------

let shopRow = 0;

function openShop(): void {
  mode = Mode.Shop;
  shopRow = 0;
  showPanel("shop");
  drawShop();
}

function drawShop(): void {
  UI.setText("shop-scrip", hero.scrip().toString());
  for (let i = 0; i < 6; i++) {
    const id = "shop-" + (i + 1).toString();
    UI.setVisible(id, i < STOCK.length, "hidden");
    if (i >= STOCK.length) continue;
    const def = item(STOCK[i].item);
    if (def == null) continue;
    UI.setAttribute(id + "-icon", "src", ATLAS + def.icon);
    UI.setText(id + "-name", def.name);
    UI.setText(id + "-price", STOCK[i].price.toString());
    UI.toggleClass(id, "selected", i == shopRow);
    UI.toggleClass(id, "dear", STOCK[i].price > hero.scrip());
    if (i == shopRow) UI.setText("shop-desc", def.description);
  }
}

function shop(): void {
  if (Input.pressed("back") || Pointer.rightClicked || (Pointer.clicked && !Pointer.over("shop"))) {
    showPanel(""); mode = Mode.Explore; play("ui_back", 0.5); refreshHud(); return;
  }
  const n = min(6, STOCK.length);
  const hovered = Pointer.overRow("shop-", n);
  if (Pointer.moved && hovered >= 0 && hovered != shopRow) { shopRow = hovered; play("ui_move", 0.3); drawShop(); }
  if (Input.repeated("up", 0.3, 0.1)) { shopRow = (shopRow + n - 1) % n; play("ui_move", 0.4); drawShop(); }
  if (Input.repeated("down", 0.3, 0.1)) { shopRow = (shopRow + 1) % n; play("ui_move", 0.4); drawShop(); }
  if (Input.pressed("confirm") || (Pointer.clicked && hovered >= 0)) {
    const s = STOCK[shopRow];
    if (hero.scrip() < s.price) { say("Not enough scrip."); play("ui_back"); return; }
    hero.addScrip(-s.price);
    hero.give(s.item);
    drainNotes();
    drawShop();
    refreshHud();
  }
}

// ---- fighting -------------------------------------------------------------------

class Foe {
  figure: Figure;
  def: EnemyDef;
  hp: i32 = 0;
  ap: i32 = 0;
  turns: i32 = 0;
  summoned: bool = false;
  constructor(public entity: Entity) {
    const d = enemy(whoOf(entity));
    const fig = figureOf(entity);
    this.figure = fig != null ? fig : new Figure(entity, "raider_down_0");
    this.def = d != null ? d : new EnemyDef();
    this.hp = this.def.hp;
  }
  get alive(): bool { return this.hp > 0; }
}

const figures: Figure[] = [];
function figureOf(e: Entity): Figure | null {
  for (let i = 0; i < figures.length; i++) if (figures[i].entity.equals(e)) return figures[i];
  return null;
}

let inFight = false;
let foes: Foe[] = [];
let ap = 0;
let turn = -1;       // -1: the hero's; else the index of the foe acting
let pause: f32 = 0;  // a beat between actions
let aiming: AbilityDef | null = null;
let targets: Foe[] = [];
let aim = 0;
let cursor = Entity.NONE;

function checkAmbush(): void {
  for (let i = 0; i < figures.length; i++) {
    const e = figures[i].entity;
    if (!e.hasTag("enemy") || !e.isAlive) continue;
    const c = figures[i].cell;
    if (dist(c, player.cell) <= 5 && grid.sees(c, player.cell)) { startCombat(e); return; }
  }
}

// Everyone hostile within reach of the hero joins.
function startCombat(first: Entity): void {
  route = [];
  routeTarget = Entity.NONE;
  foes = [];
  for (let i = 0; i < figures.length; i++) {
    const e = figures[i].entity;
    if (e.hasTag("enemy") && e.isAlive && (e.equals(first) || dist(figures[i].cell, player.cell) <= 9)) foes.push(new Foe(e));
  }
  if (foes.length == 0) return;
  inFight = true;
  mode = Mode.Combat;
  LOG.length = 0;
  play("combat_start");
  let boss = false;
  for (let i = 0; i < foes.length; i++) if (foes[i].def.ai == "boss") boss = true;
  music.fadeOut(0.4);
  fightMusic = new Music(boss ? "music_boss" : "music_combat");
  (fightMusic as Music).play(0.5);
  if (boss) play("warden_roar");
  UI.setVisible("log", true, "hidden");
  UI.setVisible("endturn", true, "hidden");
  say("Combat!");
  heroTurn();
}

// Your turn: a reticle under each foe some ready ability can reach from where you stand.
const marks: Entity[] = [];
let marksDirty = true;

function clearMarks(): void {
  for (let i = 0; i < marks.length; i++) marks[i].destroy();
  marks.length = 0;
}

function inReach(f: Foe): bool {
  for (let i = 0; i < ABILITIES.length; i++) {
    const a = ABILITIES[i];
    if (a.kind == "heal" || hero.cantUse(a.id, ap) != "") continue;
    if (dist(f.figure.cell, player.cell) <= a.range && grid.sees(player.cell, f.figure.cell)) return true;
  }
  return false;
}

function showMarks(): void {
  clearMarks();
  marksDirty = false;
  if (!inFight || turn != -1) return;
  for (let i = 0; i < foes.length; i++) {
    const f = foes[i];
    if (!f.alive || !inReach(f)) continue;
    const m = spawn("cursor", grid.worldX(f.figure.cell.x), grid.worldY(f.figure.cell.y), new Overrides().tint(1, 1, 1, 0.55));
    m.transform.z = 9;
    marks.push(m);
  }
}

function heroTurn(): void {
  turn = -1;
  marksDirty = true;
  ap = hero.maxAp();
  banner("YOUR TURN");
  refreshHud(ap);
}

function spend(n: i32): void {
  ap = max(0, ap - n);
  marksDirty = true;
  refreshHud(ap);
}

let bannerTime: f32 = 0;
function banner(text: string): void {
  UI.setText("banner", text);
  UI.setVisible("banner", true, "hidden");
  bannerTime = 1.2;
}

function float(at: Entity, text: string, r: f32, g: f32, b: f32): void {
  const t = at.transform;
  const f = spawn("floater", t.x, t.y + 12, new Overrides().velocity(0, 18).lifetime(0.9));
  f.text.set(text);
  f.text.setColor(r, g, b);
}

function effect(prefab: string, at: Entity): void {
  spawn(prefab, at.transform.x, at.transform.y, new Overrides().lifetime(0.45));
}

function hitChance(from: Cell, to: Cell, base: i32): i32 {
  return max(5, min(95, base - max(0, dist(from, to) - 1) * 4));
}

function combat(dt: f32): void {
  let busy = player.update(dt, 0.12);
  for (let i = 0; i < foes.length; i++) busy = foes[i].figure.update(dt, 0.14) || busy;
  if (pause > 0) { pause -= dt; return; }
  if (busy) return;
  if (turn == -1) heroActs(); else foeActs();
}

let steppedInFight = false;

function heroActs(): void {
  if (marksDirty) showMarks();
  if (steppedInFight) { steppedInFight = false; arrived(); if (mode != Mode.Combat) return; }
  if (aiming != null) { choosing(); return; }
  if (combatRoute.length > 0) { walkCombatRoute(); return; }
  if (pendingStrike != null) {
    const f = pendingStrike as Foe;
    pendingStrike = null;
    const a = armedAbility();
    if (f.alive && dist(f.figure.cell, player.cell) <= a.range && grid.sees(player.cell, f.figure.cell) && hero.cantUse(a.id, ap) == "") heroStrikes(a, f);
    return;
  }
  if (Input.pressed("inventory")) { openMenu(0); return; }
  if (Input.pressed("back")) { openMenu(3); return; }
  if (Input.pressed("end_turn") || ap == 0 || (Pointer.clicked && Pointer.over("endturn"))) { endHeroTurn(); return; }
  for (let i = 0; i < 4 && i < ABILITIES.length; i++) {
    if (Input.pressed("ability_" + (i + 1).toString())) { arm(ABILITIES[i]); pick(ABILITIES[i]); return; }
    if (Pointer.clicked && Pointer.over("ab-" + (i + 1).toString())) { arm(ABILITIES[i]); if (ABILITIES[i].kind == "heal") pick(ABILITIES[i]); return; }
  }
  pointAt();
  const d = direction();
  if (d != null && ap >= 1) {
    clearPreview();
    const to = new Cell(player.cell.x + d.x, player.cell.y + d.y);
    if (grid.walkable(to.x, to.y)) { player.stepTo(to); play("step", 0.25); spend(1); steppedInFight = true; }
    else player.face(to);
  }
}

// ---- the mouse in a fight: the armed ability goes off on a clicked foe; a clicked
// tile is walked to, a point of AP a step. Hovering shows which, and what it costs.

let armed: AbilityDef | null = null;
let combatRoute: Cell[] = [];
let pendingStrike: Foe | null = null;  // walked toward; struck when the walk ends
const preview: Entity[] = [];
let previewCell = new Cell(-1, -1);

function arm(a: AbilityDef): void {
  if (a.kind == "heal") return;  // used at once, never held
  armed = a;
  previewCell = new Cell(-1, -1);
  refreshHud(ap);
}

function armedAbility(): AbilityDef {
  if (armed != null && hero.knows((armed as AbilityDef).id)) return armed as AbilityDef;
  return ABILITIES[0];  // Strike
}

function clearPreview(): void {
  for (let i = 0; i < preview.length; i++) preview[i].destroy();
  preview.length = 0;
  previewCell = new Cell(-1, -1);
  UI.setVisible("target", false, "hidden");
}

function foeAt(c: Cell): Foe | null {
  for (let i = 0; i < foes.length; i++) if (foes[i].alive && foes[i].figure.cell.equals(c)) return foes[i];
  return null;
}

function pointAt(): void {
  const c = pointerCell();
  if (c == null) { if (preview.length > 0 || previewCell.x >= 0) clearPreview(); return; }
  const cell = c as Cell;
  const foe = foeAt(cell);
  if (!cell.equals(previewCell)) {
    clearPreview();
    previewCell = cell;
    if (foe != null) {
      showFoe(foe as Foe, armedAbility());
    } else if (!cell.equals(player.cell) && grid.walkable(cell.x, cell.y)) {
      // The way there, green as far as the AP go, red past it.
      // Green as far as the AP go, red past it; burning steps in orange.
      const path = grid.pathToward(player.cell, cell, 0);
      let burns = 0;
      for (let i = 0; i < path.length; i++) {
        const ok = i < ap;
        const hot = grid.hazard(path[i]);
        if (hot && ok) burns++;
        const m = spawn("cursor", grid.worldX(path[i].x), grid.worldY(path[i].y),
                        new Overrides().texture(ATLAS + "mark_path").tint(!ok ? 1 : hot ? 1 : 0.6, !ok ? 0.4 : hot ? 0.6 : 1, !ok ? 0.4 : hot ? 0.2 : 0.6, 0.8));
        m.transform.z = 9;
        preview.push(m);
      }
      if (path.length > 0) {
        hint("Walk: " + path.length.toString() + " AP" + (path.length > ap ? " (you have " + ap.toString() + ")" : "") +
             (burns > 0 ? ", " + burns.toString() + " burning" : ""));
      }
    }
  }
  if (!Pointer.clicked) return;
  if (foe != null) {
    const a = armedAbility();
    const f = foe as Foe;
    const why = hero.cantUse(a.id, ap);
    if (why.length > 0) { say(a.name + ": " + why); play("ui_back"); return; }
    clearPreview();
    if (dist(f.figure.cell, player.cell) <= a.range && grid.sees(player.cell, f.figure.cell)) { heroStrikes(a, f); return; }
    // Out of reach: walk into range first, if the AP cover both.
    const approach = grid.pathToward(player.cell, f.figure.cell, a.range);
    if (approach.length == 0 || approach.length + a.ap > ap) {
      say(a.name + (approach.length == 0 ? " can't reach " + f.def.name : ": needs " + (approach.length + a.ap).toString() + " AP to get there"));
      play("ui_back");
      return;
    }
    combatRoute = approach;
    pendingStrike = f;
    return;
  }
  const path = grid.pathToward(player.cell, cell, 0);
  if (path.length == 0) return;
  if (path.length > ap) { hint("Too far: " + path.length.toString() + " AP, you have " + ap.toString()); play("ui_back"); return; }
  clearPreview();
  combatRoute = path;
}

function walkCombatRoute(): void {
  if (Pointer.rightClicked || Input.pressed("back")) { combatRoute = []; pendingStrike = null; return; }
  const next = combatRoute.shift();
  if (ap < 1 || !grid.walkable(next.x, next.y)) { combatRoute = []; pendingStrike = null; return; }
  player.stepTo(next);
  play("step", 0.25);
  spend(1);
  steppedInFight = true;
}

// The target panel for a foe: its health and the armed ability's odds from here.
function showFoe(f: Foe, a: AbilityDef): void {
  UI.setVisible("target", true, "hidden");
  UI.setText("target-name", f.def.name);
  bar("target-fill", <f32>f.hp / <f32>f.def.hp);
  UI.setText("target-hp", f.hp.toString() + "/" + f.def.hp.toString());
  const reach = dist(f.figure.cell, player.cell) <= a.range && grid.sees(player.cell, f.figure.cell);
  const why = hero.cantUse(a.id, ap);
  let text = a.name + ": " + why;
  if (why.length == 0 && reach) {
    text = a.name + "  " + hitChance(player.cell, f.figure.cell, a.kind == "melee" ? 90 : 85).toString() + "%  (" + a.ap.toString() + " AP)";
  } else if (why.length == 0) {
    const approach = grid.pathToward(player.cell, f.figure.cell, a.range);
    text = approach.length == 0 ? a.name + ": can't reach"
         : a.name + ": walk " + approach.length.toString() + " + " + a.ap.toString() + " AP" + (approach.length + a.ap > ap ? " (too far)" : "");
  }
  UI.setText("target-hit", text);
}

function pick(a: AbilityDef): void {
  const why = hero.cantUse(a.id, ap);
  if (why.length > 0) { say(why); play("ui_back"); return; }
  if (a.kind == "heal") {
    spend(a.ap);
    hero.heal(a.power);
    play(a.sound);
    effect("fx_heal", me);
    float(me, "+" + a.power.toString(), 0.4, 1, 0.5);
    refreshHud(ap);
    pause = 0.4;
    return;
  }
  targets = [];
  for (let i = 0; i < foes.length; i++) {
    const f = foes[i];
    if (f.alive && dist(f.figure.cell, player.cell) <= a.range && grid.sees(player.cell, f.figure.cell)) targets.push(f);
  }
  if (targets.length == 0) { say("No one in range (" + a.range.toString() + ")"); play("ui_back"); return; }
  aiming = a;
  aim = 0;
  play("ui_select", 0.5);
  cursor = spawn("cursor", 0, 0);
  showTarget();
}

function showTarget(): void {
  const f = targets[aim];
  cursor.transform.setPosition(grid.worldX(f.figure.cell.x), grid.worldY(f.figure.cell.y));
  cursor.transform.z = 30;
  player.face(f.figure.cell);
  showFoe(f, aiming as AbilityDef);
}

function stopAiming(): void {
  aiming = null;
  if (!cursor.isNone) cursor.destroy();
  cursor = Entity.NONE;
  UI.setVisible("target", false, "hidden");
}

function choosing(): void {
  if (Input.pressed("back")) { stopAiming(); play("ui_back", 0.5); return; }
  if (Input.pressed("next_target") || Input.repeated("right", 0.3, 0.15) || Input.repeated("up", 0.3, 0.15)) { aim = (aim + 1) % targets.length; play("ui_move", 0.4); showTarget(); }
  if (Input.repeated("left", 0.3, 0.15) || Input.repeated("down", 0.3, 0.15)) { aim = (aim + targets.length - 1) % targets.length; play("ui_move", 0.4); showTarget(); }
  if (Input.pressed("confirm")) {
    const a = aiming as AbilityDef;
    const f = targets[aim];
    stopAiming();
    heroStrikes(a, f);
  }
}

function heroStrikes(a: AbilityDef, f: Foe): void {
  previewCell = new Cell(-1, -1);
  spend(a.ap);
  if (a.consumes.length > 0) hero.take(a.consumes);
  play(a.sound);
  const chance = hitChance(player.cell, f.figure.cell, a.kind == "melee" ? 90 : 85);
  pause = 0.5;
  if (Random.int(1, 100) > chance) {
    play("miss", 0.6);
    float(f.entity, "miss", 0.8, 0.8, 0.8);
    say(a.name + " misses.");
    return;
  }
  let damage = a.power + (a.kind == "melee" ? hero.weaponBonus() : 0) + Random.int(-1, 2);
  const crit = Random.int(1, 100) <= 10;
  if (crit) damage = damage * 3 / 2;
  if (a.kind != "ember") damage -= f.def.armor;  // ember ignores armor
  damage = max(1, damage);
  effect(a.kind == "ember" ? "fx_burst" : "fx_hit", f.entity);
  play(crit ? "crit" : "hit", 0.6);
  hurt(f, damage, crit);
  say(a.name + " hits " + f.def.name + " for " + damage.toString() + (crit ? ". Critical!" : "."));
}

function hurt(f: Foe, damage: i32, crit: bool): void {
  f.hp -= damage;
  float(f.entity, damage.toString() + (crit ? "!" : ""), 1, crit ? 0.8 : 0.4, 0.2);
  f.entity.sprite.setColor(1, 0.4, 0.4);
  flash.push(f.entity);
  flashTime = 0.15;
  if (f.hp > 0) return;
  play("death", 0.6);
  say(f.def.name + " falls.");
  GameState.setNumber("gone." + keyOf(f.entity), 1);
  f.entity.destroy();
  marksDirty = true;
  hero.gainXp(f.def.xp);
  if (f.def.loot.length > 0) hero.give(f.def.loot);
  drainNotes();
  checkWon();
}

const flash: Entity[] = [];
let flashTime: f32 = 0;

function checkWon(): void {
  for (let i = 0; i < foes.length; i++) if (foes[i].alive) return;
  clearMarks();
  clearPreview();
  combatRoute = [];
  inFight = false;
  mode = Mode.Explore;
  turn = -1;
  if (fightMusic != null) (fightMusic as Music).fadeOut(1);
  music.play(0.5);
  play("quest", 0.5);
  banner("VICTORY");
  UI.setVisible("log", false, "hidden");
  UI.setVisible("endturn", false, "hidden");
  hero.advanceQuests();
  drainNotes();
  refreshHud();
}

function endHeroTurn(): void {
  stopAiming();
  clearMarks();
  clearPreview();
  combatRoute = [];
  pendingStrike = null;
  turn = 0;
  banner("ENEMY TURN");
  refreshHud(0);
  startFoe();
}

function startFoe(): void {
  while (turn < foes.length && !foes[turn].alive) turn++;
  if (turn >= foes.length) { heroTurn(); return; }
  const f = foes[turn];
  f.ap = f.def.ap;
  f.turns++;
  pause = 0.3;
  // The Warden calls for help once it's hurt.
  if (f.def.ai == "boss" && !f.summoned && f.hp * 2 < f.def.hp) {
    f.summoned = true;
    summon(f);
  }
}

function summon(boss: Foe): void {
  play("warden_roar");
  say("The Warden bellows. Drones stir in the slag!");
  Camera.shake(4, 0.5);
  const spots = [new Cell(-2, 0), new Cell(2, 0), new Cell(0, -2), new Cell(0, 2), new Cell(-2, -2), new Cell(2, 2)];
  let made = 0;
  for (let i = 0; i < spots.length && made < 2; i++) {
    const c = new Cell(boss.figure.cell.x + spots[i].x, boss.figure.cell.y + spots[i].y);
    if (!grid.walkable(c.x, c.y)) continue;
    const e = spawn("foe", grid.worldX(c.x), grid.worldY(c.y) + LIFT,
                    new Overrides().paramText("who", "scrap_drone").paramText("key", "summoned").texture(ATLAS + "drone_0"));
    pendingSummons.push(new Pending(e, c));
    made++;
  }
}

class Pending { constructor(public entity: Entity, public cell: Cell) {} }
const pendingSummons: Pending[] = [];

function adoptSummons(): void {
  for (let i = pendingSummons.length - 1; i >= 0; i--) {
    const p = pendingSummons[i];
    if (!p.entity.isAlive) continue;
    const fig = new Figure(p.entity, "drone_0");
    fig.cell = p.cell;
    grid.placeAt(p.entity, p.cell);
    figures.push(fig);
    if (inFight) foes.push(new Foe(p.entity));
    pendingSummons.splice(i, 1);
  }
}

function foeActs(): void {
  if (turn >= foes.length) { heroTurn(); return; }
  const f = foes[turn];
  if (!f.alive) { turn++; startFoe(); return; }
  const d = dist(f.figure.cell, player.cell);
  const inReach = d <= f.def.range && grid.sees(f.figure.cell, player.cell);
  const cost = 4;  // with 6 AP: one shot and a step or two; the Warden's 8 buys two
  // The Warden's steam vent: every third turn, scalding everything close.
  if (f.def.ai == "boss" && f.turns % 3 == 0 && d <= 2 && f.ap >= 6) {
    f.ap = 0;
    play("ember");
    effect("fx_burst", me);
    Camera.shake(3, 0.4);
    say("Steam vents from the Warden!");
    hurtHero(6, f.def.name);
    pause = 0.8;
    return;
  }
  if (inReach && f.ap >= cost) {
    f.ap -= cost;
    f.figure.face(player.cell);
    play(f.def.sound);
    pause = 0.6;
    const chance = hitChance(f.figure.cell, player.cell, f.def.accuracy);
    if (Random.int(1, 100) > chance) {
      play("miss", 0.6);
      float(me, "miss", 0.8, 0.8, 0.8);
      say(f.def.name + " misses.");
      return;
    }
    effect("fx_hit", me);
    hurtHero(max(1, f.def.damage + Random.int(-1, 1) - hero.armor()), f.def.name);
    return;
  }
  if (!inReach && f.ap >= 1) {
    const path = grid.pathToward(f.figure.cell, player.cell, f.def.range);
    if (path.length > 0) {
      f.ap -= 1;
      f.figure.stepTo(path[0]);
      return;
    }
  }
  turn++;
  startFoe();
}

function hurtHero(damage: i32, by: string): void {
  hero.setHp(hero.hp() - damage);
  play("hit", 0.6);
  float(me, damage.toString(), 1, 0.3, 0.2);
  me.sprite.setColor(1, 0.4, 0.4);
  flash.push(me);
  flashTime = 0.15;
  Camera.shake(2, 0.2);
  say(by + " hits you for " + damage.toString() + ".");
  refreshHud(0);
  if (hero.hp() <= 0) die();
}

// ---- dying ----------------------------------------------------------------------

let overRow = 0;

function die(): void {
  clearMarks();
  clearPreview();
  mode = Mode.Over;
  inFight = false;
  stopAiming();
  if (fightMusic != null) (fightMusic as Music).fadeOut(1.5);
  play("death");
  me.sprite.setColor(0.5, 0.2, 0.2, 0.8);
  overRow = 0;
  pause = 1.2;
  showPanel("over");
  UI.setVisible("log", false, "hidden");
  UI.setVisible("endturn", false, "hidden");
  drawOver();
}

function drawOver(): void {
  UI.toggleClass("over-1", "selected", overRow == 0);
  UI.toggleClass("over-2", "selected", overRow == 1);
  UI.toggleClass("over-1", "disabled", slots.latest() == 0);
}

function over(dt: f32): void {
  if (pause > 0) { pause -= dt; return; }
  if (Input.repeated("up", 0.3, 0.15) || Input.repeated("down", 0.3, 0.15)) { overRow = 1 - overRow; play("ui_move", 0.4); drawOver(); }
  const hovered = Pointer.overRow("over-", 2);
  if (Pointer.moved && hovered >= 0 && hovered != overRow) { overRow = hovered; play("ui_move", 0.3); drawOver(); }
  if (!Input.pressed("confirm") && !(Pointer.clicked && hovered >= 0)) return;
  if (overRow == 0 && slots.latest() > 0) { loadSlot(slots.latest()); return; }
  mode = Mode.Leaving;
  Scene.transition("title", 1);
}

// ---- every frame ----------------------------------------------------------------

function follow(): void {
  const t = me.transform;
  const halfW: f32 = 240, halfH: f32 = 135;
  const w = <f32>grid.width * 16, h = <f32>grid.height * 16;
  const x = w <= halfW * 2 ? w / 2 : Mathf.max(halfW, Mathf.min(w - halfW, t.x));
  const y = h <= halfH * 2 ? h / 2 : Mathf.max(halfH, Mathf.min(h - halfH, t.y));
  Camera.setPosition(Mathf.round(x), Mathf.round(y));
}

function gather(): void {
  const tags = ["npc", "enemy", "cache"];
  for (let t = 0; t < tags.length; t++) {
    const all = World.findAll(tags[t]);
    for (let i = 0; i < all.length; i++) {
      const e = all[i];
      if (GameState.getNumber("gone." + keyOf(e)) > 0 && !e.hasTag("cache")) { e.destroy(); continue; }
      if (e.hasTag("cache")) {
        if (GameState.getNumber("gone." + keyOf(e)) > 0) e.sprite.setColor(0.6, 0.6, 0.6);
        continue;
      }
      const who = whoOf(e);
      let sprite = "";
      if (e.hasTag("npc")) { const p = person(who); sprite = p != null ? p.sprite : who + "_down_0"; }
      else { const d = enemy(who); sprite = d != null ? d.sprite : "raider_down_0"; }
      figures.push(new Figure(e, sprite));
    }
  }
  const picks = World.findAll("pickup");
  for (let i = 0; i < picks.length; i++) {
    if (GameState.getNumber("gone." + keyOf(picks[i])) > 0) picks[i].destroy();
  }
}

let started = false;

export function onUpdate(dt: f32): void {
  if (!started) {
    started = true;
    gather();
    arrive();
  }
  GameState.add("time.played", dt);
  Pointer.update();
  adoptSummons();
  if (toastTime > 0) { toastTime -= dt; if (toastTime <= 0) UI.setVisible("toast", false, "hidden"); }
  if (bannerTime > 0) { bannerTime -= dt; if (bannerTime <= 0) UI.setVisible("banner", false, "hidden"); }
  if (flashTime > 0) {
    flashTime -= dt;
    if (flashTime <= 0) {
      for (let i = 0; i < flash.length; i++) if (flash[i].isAlive) flash[i].sprite.setColor(1, 1, 1);
      flash.length = 0;
    }
  }
  const placeAge = GameState.getNumber("time.played") - placeShown;
  if (placeAge > 3) UI.setVisible("place", false, "hidden");
  for (let i = 0; i < figures.length; i++) if (figures[i].entity.isAlive && !inFight) figures[i].update(dt);

  if (mode == Mode.Explore) explore(dt);
  else if (mode == Mode.Talk) talking(dt);
  else if (mode == Mode.Menu) menu();
  else if (mode == Mode.Shop) shop();
  else if (mode == Mode.Combat) combat(dt);
  else if (mode == Mode.Over) over(dt);
  drainNotes();
  follow();
}
const placeShown = GameState.getNumber("time.played");
