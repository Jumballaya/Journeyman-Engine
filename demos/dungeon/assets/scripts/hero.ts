// Wren: starts where the scene places her (or below the stairs, or at a test
// warp), walks the rooms, swings the sword, talks, opens locked doors, takes
// the stairs, and gets hurt. Publishes its room (data "roomX"/"roomY") so the
// area can scroll the camera; walks a few steps into each new room meanwhile.
import { Entity, Input, Scene, Sound, TileBody, TileMap, Time, World, self, spawn } from "@jm/runtime";
import { areaById } from "./lib/areas";
import { Arrival, Session, placeKey } from "./lib/session";
import { CORNER_SLIDE, center, roomX, roomY } from "./lib/tiles";

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

let rx = 0, ry = 0;
let placed = false;

// On the first frame, once every entity is in: arrivals move off the authored spot.
function place(): void {
  body.x = me.transform.x;
  body.y = me.transform.y;
  const warp = Session.takeWarp();
  if (warp.length == 2) {
    body.x = center(warp[0]);
    body.y = center(warp[1]);
  } else if (Session.arrival == Arrival.Stairs) {
    const stairs = map.positionsOf("stairs_down");
    if (stairs.length >= 2) {
      body.x = center(stairs[0]);
      body.y = center(stairs[1] - 2);
    }
  }
  me.transform.setPosition(body.x, body.y);
  rx = roomX(body.x);
  ry = roomY(body.y);
  publishRoom();
}

// Fires and the hermit stand in the way.
function blocked(x: f32, y: f32): bool {
  const all = World.findAll("solid");
  for (let i = 0; i < all.length; i++) {
    const t = all[i].transform;
    if (Mathf.abs(t.x - x) < 8 + body.halfW && Mathf.abs(t.y - y) < 8 + body.halfH) return true;
  }
  return false;
}

function publishRoom(): void {
  me.data.setNumber("roomX", rx);
  me.data.setNumber("roomY", ry);
}

function dirX(f: Facing): f32 { return f == Facing.Left ? -1 : f == Facing.Right ? 1 : 0; }
function dirY(f: Facing): f32 { return f == Facing.Down ? -1 : f == Facing.Up ? 1 : 0; }

function animate(moving: bool): void {
  const name = facing == Facing.Down ? "down" : facing == Facing.Up ? "up" : "side";
  me.sprite.play(name + (moving ? "_walk" : "_idle"));
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
  if (map.at(tx, ty) != "door" || Session.keys == 0) return;
  Session.keys = Session.keys - 1;
  for (let x = tx - 1; x <= tx + 1; x++) {
    for (let y = ty - 1; y <= ty + 1; y++) {
      if (map.at(x, y) != "door") continue;
      Session.markDone(placeKey(area.id, x, y));
      map.set(x, y, "");
    }
  }
  new Sound("door").play(0.7);
}

// Steps onto stairs take the hero to the other area.
function checkStairs(): void {
  const c = map.at(map.tileX(body.x), map.tileY(body.y));
  if (c != "stairs_down" && c != "stairs_up") return;
  leaving = true;
  Session.area = c == "stairs_down" ? "crypt" : "grove";
  Session.arrival = c == "stairs_down" ? Arrival.Start : Arrival.Stairs;
  new Sound("stairs").play(0.6);
  Scene.transition(Session.area, 0.6);
}

function walk(dt: f32): void {
  let ix = Input.axis("left", "right"), iy = Input.axis("down", "up");
  if (Mathf.abs(ix) > Mathf.abs(iy)) facing = ix > 0 ? Facing.Right : Facing.Left;
  else if (iy != 0) facing = iy > 0 ? Facing.Up : Facing.Down;
  const len = Mathf.sqrt(ix * ix + iy * iy);
  if (len > 1) { ix /= len; iy /= len; }
  const x0 = body.x, y0 = body.y;
  body.move(map, ix * SPEED * dt, iy * SPEED * dt, CORNER_SLIDE);
  if (blocked(body.x, body.y)) {
    if (!blocked(body.x, y0)) body.y = y0;
    else if (!blocked(x0, body.y)) body.x = x0;
    else { body.x = x0; body.y = y0; }
  }
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
  if (!placed) {
    place();
    placed = true;
  }
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

  if ((roomX(body.x) != rx || roomY(body.y) != ry) && scroll <= 0) {
    rx = roomX(body.x);
    ry = roomY(body.y);
    publishRoom();
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
