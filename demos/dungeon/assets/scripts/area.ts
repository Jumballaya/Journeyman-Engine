// Runs an area scene (param "area": its id, also the scene's name): scrolls the camera when the
// hero changes rooms and swaps in the room's group of enemies and items, keeps
// the HUD, music and pause, and ends the game on victory. runWhenPaused.
import { Input, Music, Params, Renderer, Scene, TileMap, Time, UI, World } from "@jm/runtime";
import { areaById } from "./lib/areas";
import { Session, placeKey } from "./lib/session";
import { roomGroup } from "./lib/tiles";
import { lookAt, roomCenterX, roomCenterY } from "./lib/view";

const ATLAS = "assets/atlases/sprites.atlas.json#";
const SCROLL_SECONDS: f32 = 0.6;
const MAX_HEARTS = 8;

if (!Session.started) Session.newGame();  // launched directly (JM_ENTRY_SCENE)
Session.area = Params.text("area", Session.area);  // param "area": this scene's area id
const area = areaById(Session.area);
const map = TileMap.find("map");
let music: Music | null = null;
let playing = "";
let roomX: i32 = -1, roomY: i32 = -1;  // none until the hero publishes its room
let fromX: f32 = 0, fromY: f32 = 0;   // camera position when a scroll began
let scroll: f32 = -1;                  // seconds into a room scroll, or -1
let paused = false;
let wonFor: f32 = 0;      // seconds since the shard was taken
let shownHealth = -1, shownGems = -1, shownKeys = -1;

Renderer.setClearColor(0, 0, 0);
openDoors();
UI.setText("area-name", area.outdoors ? "HOLLOW GROVE" : "THE CRYPT");

// Doors opened before stay open: their tiles become floor (their scene entities don't spawn).
function openDoors(): void {
  for (let ty = 0; ty < map.height; ty++) {
    for (let tx = 0; tx < map.width; tx++) {
      if (map.at(tx, ty) == "+" && Session.done(placeKey(area.id, tx, ty))) map.set(tx, ty, ".");
    }
  }
}

// Clears the old room (its group, and whatever its enemies dropped) and brings in the new one's.
function enterRoom(rx: i32, ry: i32): void {
  const old = World.findAll("foe").concat(World.findAll("loot"));
  for (let i = 0; i < old.length; i++) old[i].destroy();
  if (roomX >= 0) Scene.despawnGroup(roomGroup(roomX, roomY));
  roomX = rx;
  roomY = ry;
  Scene.spawnGroup(roomGroup(rx, ry));
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
  if (roomX < 0) {  // the first room: straight there
    enterRoom(rx, ry);
    lookAt(roomCenterX(roomX), roomCenterY(roomY));
    return;
  }
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
  playMusic(World.find("boss").isNone ? area.music : "music_boss");
  if (!Session.won) return;
  if (wonFor == 0 && music !== null) music!.fadeOut(1);
  const before = wonFor;
  wonFor += dt;
  if (before <= 3 && wonFor > 3) Scene.transition("victory", 1.0);
}
