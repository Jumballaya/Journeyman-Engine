import { CameraFollow, GameState, Input, TileMap, self } from "@jm/runtime";
import { CUT, JUMP, jumps, pose, run } from "./lib/moves";

const me = self();
const map = TileMap.find("Map");
const camera = new CameraFollow().setBounds(0, 0, <f32>map.width * map.tileWidth, <f32>map.height * map.tileHeight);
camera.deadZoneWidth = 80;
camera.deadZoneHeight = 80;
camera.lookAhead = 90;
camera.smoothing = 7;
let sincePressed: f32 = 99;  // seconds since jump was pressed
let sinceGround: f32 = 99;   // seconds since it stood
let spawnX: f32 = me.transform.x, spawnY: f32 = me.transform.y;
const spawn = map.object("spawn");
if (spawn !== null) { spawnX = spawn!.x; spawnY = spawn!.y + 30; }  // globals don't narrow

export function onUpdate(dt: f32): void {
  const v = me.velocity;
  sincePressed = Input.pressed("jump") ? 0 : sincePressed + dt;
  sinceGround = v.onGround ? 0 : sinceGround + dt;
  const axis = Input.axis("left", "right");
  v.x = run(v.x, axis, v.onGround, dt);
  v.dropThrough = Input.down("down") && Input.down("jump");
  if (jumps(sincePressed, sinceGround) && !v.dropThrough) {
    v.y = JUMP + v.supportVelocityY;
    sincePressed = sinceGround = 99;
  } else if (Input.released("jump") && v.y > 0) {
    v.y *= CUT;  // a tap is a hop
  }
  if (axis != 0) me.transform.setScale(axis > 0 ? 32 : -32, 32);
  me.sprite.play(pose(v.x, v.y, v.onGround));
  if (me.transform.y < -120) respawn();
  const goal = map.object("goal");
  if (goal !== null && me.transform.x > goal.x) GameState.setNumber("reachedGoal", 1);
  camera.follow(me, dt);
}

function respawn(): void {
  me.transform.setPosition(spawnX, spawnY);
  me.velocity.set(0, 0);
  camera.jumpTo(spawnX, spawnY);
  GameState.add("falls", 1);
}
