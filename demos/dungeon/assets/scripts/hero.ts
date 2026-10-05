// Wren: walks the rooms, swings the sword, talks, opens locked doors, takes
// the stairs, and gets hurt. Publishes the current room (Session.room) so the
// area can scroll the camera; walks a few steps into each new room meanwhile.
import { Entity, Input, Scene, Sound, TileBody, TileMap, Time, World, self, spawn } from "@jm/runtime";
import { areaById } from "./lib/areas";
import { Arrival, Session, placeKey } from "./lib/session";
import { CORNER_SLIDE, roomX, roomY } from "./lib/tiles";

const SPEED: f32 = 80;
const SWING_SECONDS: f32 = 0.22;
const SCROLL_SECONDS: f32 = 0.6;   // matches the area's camera scroll
const HURT_SECONDS: f32 = 1.0;
const KNOCKBACK: f32 = 180;

enum Facing { Down, Up, Left, Right }

const me = self();
const area = areaById(Session.area);
const map = TileMap.find("map");
const body = new TileBody(5, 5);
let facing = Facing.Down;
let swing: f32 = 0;               // seconds left in a sword swing
let sword: Entity = Entity.NONE;
let scroll: f32 = 0;              // seconds left walking into a new room
let scrollX: f32 = 0, scrollY: f32 = 0;
let hurt: f32 = 0;                // seconds of invulnerability left
let knockX: f32 = 0, knockY: f32 = 0;
let leaving = false;
let shown = "";

body.x = me.transform.x;
body.y = me.transform.y;
Session.room = roomOf(body.x, body.y);

function roomOf(x: f32, y: f32): string {
  return roomX(x).toString() + "," + roomY(y).toString();
}

function dirX(f: Facing): f32 { return f == Facing.Left ? -1 : f == Facing.Right ? 1 : 0; }
function dirY(f: Facing): f32 { return f == Facing.Down ? -1 : f == Facing.Up ? 1 : 0; }

function playOnce(animation: string): void {  // Sprite.play restarts; only switch on change
  if (animation == shown) return;
  shown = animation;
  me.sprite.play(animation);
}

function animate(moving: bool): void {
  const name = facing == Facing.Down ? "down" : facing == Facing.Up ? "up" : "side";
  playOnce(name + (moving ? "_walk" : "_idle"));
  me.transform.scaleX = facing == Facing.Left ? -8 : 8;
  me.sprite.alpha = hurt > 0 && <i32>Mathf.floor(hurt * 16) % 2 == 0 ? 0.3 : 1;
}

// Talks to an NPC just ahead, or swings the sword.
function act(): void {
  const npc = World.find("npc");
  if (!npc.isNone) {
    const ax = body.x + dirX(facing) * 12, ay = body.y + dirY(facing) * 12;
    if (Mathf.abs(npc.transform.x - ax) < 12 && Mathf.abs(npc.transform.y - ay) < 12) {
      npc.send("talk");
      return;
    }
  }
  if (!Session.hasSword) return;
  swing = SWING_SECONDS;
  sword = spawn("sword", body.x, body.y);
  new Sound("swing").play(0.5);
}

function placeSword(): void {
  const dx = dirX(facing), dy = dirY(facing);
  sword.transform.setPosition(body.x + dx * 13, body.y + dy * 13 - (dx != 0 ? 2 : 0));
  sword.transform.rotation = facing == Facing.Up ? 0 : facing == Facing.Down ? Mathf.PI : facing == Facing.Left ? Mathf.PI / 2 : -Mathf.PI / 2;
}

// A locked door just walked into opens with a key (both halves of a double door).
function tryDoor(): void {
  const tx = body.hitTileX, ty = body.hitTileY;
  if (map.at(tx, ty) != "+" || Session.keys == 0) return;
  Session.keys = Session.keys - 1;
  for (let x = tx - 1; x <= tx + 1; x++) {
    for (let y = ty - 1; y <= ty + 1; y++) {
      if (map.at(x, y) != "+") continue;
      Session.markDone(placeKey(area.id, x, y));
      map.set(x, y, ".");
    }
  }
  new Sound("door").play(0.7);
}

// Steps onto stairs take the hero to the other area.
function checkStairs(): void {
  const c = map.at(map.tileX(body.x), map.tileY(body.y));
  if (c != "S" && c != "U") return;
  leaving = true;
  Session.area = c == "S" ? "crypt" : "grove";
  Session.arrival = c == "S" ? Arrival.Start : Arrival.Stairs;
  new Sound("stairs").play(0.6);
  Scene.transition(Session.area, 0.6);
}

function walk(dt: f32): void {
  let ix = Input.axis("left", "right"), iy = Input.axis("down", "up");
  if (Mathf.abs(ix) > Mathf.abs(iy)) facing = ix > 0 ? Facing.Right : Facing.Left;
  else if (iy != 0) facing = iy > 0 ? Facing.Up : Facing.Down;
  const len = Mathf.sqrt(ix * ix + iy * iy);
  if (len > 1) { ix /= len; iy /= len; }
  body.move(map, ix * SPEED * dt, iy * SPEED * dt, CORNER_SLIDE);
  if (body.blocked) tryDoor();
  animate(len > 0.1);
  if (Input.pressed("attack")) act();
}

function die(): void {
  leaving = true;
  new Sound("die").play(0.7);
  Scene.transition("game_over", 1.5);
}

export function onUpdate(dt: f32): void {
  if (leaving || Session.won || Time.paused) return;
  hurt -= dt;

  if (scroll > 0) {  // walking into the new room while the camera scrolls
    scroll -= dt;
    body.x += scrollX * dt;
    body.y += scrollY * dt;
    animate(true);
  } else if (swing > 0) {
    swing -= dt;
    placeSword();
    animate(false);
  } else if (hurt > HURT_SECONDS - 0.15) {
    body.move(map, knockX * dt, knockY * dt, CORNER_SLIDE);
  } else {
    walk(dt);
  }
  me.transform.setPosition(body.x, body.y);

  const room = roomOf(body.x, body.y);
  if (room != Session.room && scroll <= 0) {
    Session.room = room;
    scroll = SCROLL_SECONDS;
    scrollX = dirX(facing) * 28 / SCROLL_SECONDS;  // 28px: clear of the doorway
    scrollY = dirY(facing) * 28 / SCROLL_SECONDS;
  }
  if (scroll <= 0) checkStairs();
}

export function onCollide(other: Entity): void {
  if (!other.hasTag("foe") || hurt > 0 || scroll > 0 || leaving) return;
  Session.health = Session.health - (other.hasTag("boss") ? 2 : 1);
  hurt = HURT_SECONDS;
  const dx = body.x - other.transform.x, dy = body.y - other.transform.y;
  const len = Mathf.max(Mathf.sqrt(dx * dx + dy * dy), 0.001);
  knockX = dx / len * KNOCKBACK;
  knockY = dy / len * KNOCKBACK;
  new Sound("hurt").play(0.6);
  if (other.hasTag("orb")) other.destroy();
  if (Session.health <= 0) die();
}
