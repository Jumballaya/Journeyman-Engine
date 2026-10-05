// Kael, leading the party on the map: walks, talks to whatever is ahead
// (sends it "talk"), takes the exits between maps, and steps into random
// battles in tall grass.
import { Entity, GameState, Input, Random, Scene, Sound, TileBody, TileMap, Time, World, self, spawn } from "@jm/runtime";
import { ENCOUNTERS } from "./lib/data";
import { mapById } from "./lib/maps";
import { Party } from "./lib/party";
import { FIGURE_LIFT } from "./lib/tiles";

const SPEED: f32 = 72;
const REACH: f32 = 14;   // how far ahead the hero can talk
const CORNER_SLIDE: f32 = 6;  // pixels of misalignment forgiven at openings

enum Facing { Down, Up, Left, Right }

const me = self();
const gameMap = mapById(Party.map);
const map = TileMap.find("map");
const body = new TileBody(5, 4);
const shadow = spawn("shadow", me.transform.x, me.transform.y - 6);
let facing = Facing.Down;
let leaving = false;
let untilBattle: f32 = Random.range(280, 640);  // pixels walked in tall grass before a fight

body.x = me.transform.x;
body.y = me.transform.y;
me.transform.y = body.y + FIGURE_LIFT;

function dirX(): f32 { return facing == Facing.Left ? -1 : facing == Facing.Right ? 1 : 0; }
function dirY(): f32 { return facing == Facing.Down ? -1 : facing == Facing.Up ? 1 : 0; }

function animate(moving: bool): void {
  const name = facing == Facing.Down ? "down" : facing == Facing.Up ? "up" : "side";
  const animation = name + (moving ? "_walk" : "_idle");
  me.sprite.play(animation);
  me.transform.scaleX = facing == Facing.Left ? -8 : 8;
}

// Whatever interactable stands right ahead, or Entity.NONE.
function ahead(): Entity {
  const all = World.findAll("interact");
  const ax = body.x + dirX() * REACH, ay = body.y + dirY() * REACH;
  for (let i = 0; i < all.length; i++) {
    const t = all[i].transform;
    const feet = all[i].hasTag("figure") ? t.y - FIGURE_LIFT : t.y;
    if (Mathf.abs(t.x - ax) < 10 && Mathf.abs(feet - ay) < 10) return all[i];
  }
  return Entity.NONE;
}

function travel(to: string, exit: string): void {
  Party.arriveAt(to, exit);
  leaving = true;
  Scene.transition("map", 0.5);
}

function startBattle(): void {
  leaving = true;
  Party.placeAt(gameMap.id, body.x, body.y);
  GameState.setNumber("encounter", Random.int(0, ENCOUNTERS.length - 1));
  new Sound("encounter").play(0.6);
  Scene.transition("battle", 1.0, "swirl");
}

export function onUpdate(dt: f32): void {
  if (leaving || Time.paused) return;
  let ix = Input.axis("left", "right"), iy = Input.axis("down", "up");
  if (Mathf.abs(ix) > Mathf.abs(iy)) facing = ix > 0 ? Facing.Right : Facing.Left;
  else if (iy != 0) facing = iy > 0 ? Facing.Up : Facing.Down;
  const len = Mathf.sqrt(ix * ix + iy * iy);
  if (len > 1) { ix /= len; iy /= len; }
  const x0 = body.x, y0 = body.y;
  body.move(map, ix * SPEED * dt, iy * SPEED * dt, CORNER_SLIDE);
  me.transform.setPosition(body.x, body.y + FIGURE_LIFT);
  shadow.transform.setPosition(body.x, body.y - 6);
  animate(len > 0.1);

  if (Input.pressed("confirm")) {
    const target = ahead();
    if (!target.isNone) target.send("talk");
  }

  const here = map.at(map.tileX(body.x), map.tileY(body.y));
  if (here == ">") travel("field", "<");
  else if (here == "<") travel("town", ">");
  else if (gameMap.encounters && here == "\"") {
    untilBattle -= Mathf.abs(body.x - x0) + Mathf.abs(body.y - y0);
    if (untilBattle <= 0) startBattle();
  }
}
