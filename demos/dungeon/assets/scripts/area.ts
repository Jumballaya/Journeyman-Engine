// Runs the current area (Session.area, the scene): spawns what its map marks, scrolls the camera
// when the hero changes rooms and swaps the room's enemies and items, keeps
// the HUD, music and pause, and ends the game on victory. runWhenPaused.
import { Input, Music, Overrides, Params, Renderer, Scene, TileMap, Time, UI, World, spawn } from "@jm/runtime";
import { ROOM_W, ROOM_H, areaById } from "./lib/areas";
import { Arrival, Session, placeKey } from "./lib/session";
import { center, roomX as roomOfX, roomY as roomOfY } from "./lib/tiles";
import { lookAt, roomCenterX, roomCenterY } from "./lib/view";

const ATLAS = "assets/atlases/sprites.atlas.json#";
const SCROLL_SECONDS: f32 = 0.6;
const MAX_HEARTS = 8;

if (!Session.started) Session.newGame();  // launched directly (JM_ENTRY_SCENE)
// Warp params for testing and level design: "area", start tile "tx"/"ty", "sword", "keys", "hearts".
if (Params.text("area").length > 0) Session.area = Params.text("area");
if (Params.number("sword") > 0) Session.hasSword = true;
if (Params.number("keys") > 0) Session.keys = <i32>Params.number("keys");
for (let i = Session.maxHearts; i < <i32>Params.number("hearts"); i++) Session.addHeartContainer();
const area = areaById(Session.area);
const map = TileMap.find("map");
let music: Music | null = null;
let playing = "";
let roomX: i32 = 0, roomY: i32 = 0;
let fromX: f32 = 0, fromY: f32 = 0;   // camera position when a scroll began
let scroll: f32 = -1;                  // seconds into a room scroll, or -1
let paused = false;
let wonFor: f32 = 0;      // seconds since the shard was taken
let shownHealth = -1, shownGems = -1, shownKeys = -1;

Renderer.setClearColor(0, 0, 0);
placeDoorsAndFires();
const start = arrivalTile();
spawn("hero", center(start.tx), center(start.ty));
enterRoom(roomOfX(center(start.tx)), roomOfY(center(start.ty)));
lookAt(roomCenterX(roomX), roomCenterY(roomY));
UI.setText("area-name", area.outdoors ? "HOLLOW GROVE" : "THE CRYPT");

class Spot {
  constructor(readonly tx: i32, readonly ty: i32) {}
}

// Where the hero appears: a warp tile, the area's P, or just below its stairs.
function arrivalTile(): Spot {
  if (Params.number("tx", -1) >= 0) return new Spot(<i32>Params.number("tx"), <i32>Params.number("ty"));
  for (let ty = 0; ty < map.height; ty++) {
    for (let tx = 0; tx < map.width; tx++) {
      const c = map.at(tx, ty);
      if (Session.arrival == Arrival.Start && c == "P") return new Spot(tx, ty);
      if (Session.arrival == Arrival.Stairs && c == "S") return new Spot(tx, ty - 2);
    }
  }
  return new Spot(1, 1);
}

// Fires, the hermit and locked doors (opened ones stay open: open floor).
function placeDoorsAndFires(): void {
  for (let ty = 0; ty < map.height; ty++) {
    for (let tx = 0; tx < map.width; tx++) {
      const c = map.at(tx, ty);
      const x = center(tx), y = center(ty);
      if (c == "F") spawn("fire", x, y);
      else if (c == "H") spawn("hermit", x, y);
      else if (c == "+" && Session.done(placeKey(area.id, tx, ty))) map.set(tx, ty, ".");
      else if (c == "+") spawn("door", x, y, new Overrides().paramText("place", placeKey(area.id, tx, ty)));
    }
  }
}

// Clears the old room's enemies and loose items and populates the new one.
function enterRoom(rx: i32, ry: i32): void {
  roomX = rx;
  roomY = ry;
  const old = World.findAll("foe").concat(World.findAll("loot"));
  for (let i = 0; i < old.length; i++) old[i].destroy();

  let bossRoom = false;
  for (let ty = ry * ROOM_H; ty < (ry + 1) * ROOM_H; ty++) {
    for (let tx = rx * ROOM_W; tx < (rx + 1) * ROOM_W; tx++) {
      const c = map.at(tx, ty);
      const x = center(tx), y = center(ty);
      const place = placeKey(area.id, tx, ty);
      if (c == "e") spawn("slime", x, y);
      else if (c == "b") spawn("bat", x, y);
      else if (c == "k") spawn("skull", x, y);
      else if ((c == "y" || c == "h" || c == "$") && !Session.done(place)) {
        spawn(c == "y" ? "key" : c == "h" ? "container" : "gem", x, y, new Overrides().paramText("place", place));
      } else if (c == "O") {
        bossRoom = true;
        if (!Session.done("boss")) spawn("boss", x, y, shardAt());
      } else if (c == "X" && Session.done("boss") && !Session.done(place)) {
        spawn("shard", x, y, new Overrides().paramText("place", place));
      }
    }
  }
  playMusic(bossRoom && !Session.done("boss") ? "music_boss" : area.music);
}

function shardAt(): Overrides {
  const o = new Overrides();
  for (let ty = 0; ty < map.height; ty++) {
    for (let tx = 0; tx < map.width; tx++) {
      if (map.at(tx, ty) == "X") o.param("sx", center(tx)).param("sy", center(ty));
    }
  }
  return o;
}

function playMusic(name: string): void {
  if (name == playing) return;
  if (music !== null) music!.fadeOut(0.3);
  const next = new Music(name);
  next.play(0.6);
  music = next;
  playing = name;
}

function updateHud(): void {
  if (Session.health != shownHealth) {
    shownHealth = Session.health;
    for (let i = 0; i < MAX_HEARTS; i++) {
      const id = "h" + (i + 1).toString();
      UI.setVisible(id, i < Session.maxHearts, "hidden");
      const halves = Session.health - i * 2;
      UI.setAttribute(id, "src", ATLAS + (halves >= 2 ? "heart" : halves == 1 ? "heart_half" : "heart_empty"));
    }
  }
  if (Session.gems != shownGems) UI.setText("gems", (shownGems = Session.gems).toString());
  if (Session.keys != shownKeys) UI.setText("keys", (shownKeys = Session.keys).toString());
}

// Follows the room the hero publishes (its data): a short slide to the new room.
function followRoom(dt: f32): void {
  const hero = World.find("hero").data;
  if (!hero.has("roomX")) return;  // the hero publishes its room from its first frame on
  const rx = <i32>hero.getNumber("roomX"), ry = <i32>hero.getNumber("roomY");
  if ((rx != roomX || ry != roomY) && scroll < 0) {
    fromX = roomCenterX(roomX);
    fromY = roomCenterY(roomY);
    scroll = 0;
    enterRoom(rx, ry);
  }
  if (scroll < 0) return;
  scroll += dt;
  const k = Mathf.min(scroll / SCROLL_SECONDS, 1);
  lookAt(fromX + (roomCenterX(roomX) - fromX) * k, fromY + (roomCenterY(roomY) - fromY) * k);
  if (k >= 1) scroll = -1;
}

export function onUpdate(dt: f32): void {
  if (Input.pressed("pause") && (paused || !Time.paused)) {  // not while a dialog holds the pause
    paused = !paused;
    if (paused) Time.pause();
    else Time.resume();
    UI.setVisible("pause", paused, "hidden");
  }
  updateHud();
  if (Time.paused) return;
  followRoom(dt);
  if (!Session.won) return;
  if (wonFor == 0 && music !== null) music!.fadeOut(1);
  const before = wonFor;
  wonFor += dt;
  if (before <= 3 && wonFor > 3) Scene.transition("victory", 1.0);
}
