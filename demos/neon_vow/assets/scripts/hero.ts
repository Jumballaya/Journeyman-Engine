import { Camera, CameraFollow, Entity, GameState, Input, Swing, TileMap, self, spawn } from "@jm/runtime";
import { CUT, JUMP, jumps, pose, run, stomps } from "./lib/moves";

const me = self();
const map = TileMap.find("Map");
Camera.zoom = 1.5;  // room for the skyline and the jump arc
const camera = new CameraFollow().setBounds(0, 0, <f32>map.width * map.tileWidth, <f32>map.height * map.tileHeight);
camera.deadZoneWidth = 80;
camera.deadZoneHeight = 80;
camera.lookAhead = 90;
camera.smoothing = 7;
const FEET: f32 = 21;  // from the transform down to the collider's bottom (scene: offset -5, half 16)
const SENTRY_TOP: f32 = 10;  // from a sentry's transform up to its collider's top (prefab: offset -2, half 12)
const GRAVITY: f32 = me.velocity.accelerationY;
let sincePressed: f32 = 99;  // seconds since jump was pressed
let sinceGround: f32 = 99;   // seconds since it stood
let spawnX: f32 = me.transform.x, spawnY: f32 = me.transform.y;
const spawnPoint = map.object("spawn");
if (spawnPoint !== null) { spawnX = spawnPoint!.x; spawnY = spawnPoint!.y + FEET; }  // globals don't narrow
const checkpoints = map.objects("checkpoint");  // passed ones save your place (session "checkpoint": how many)
let safeFor: f32 = 0;        // seconds Kage can't be hurt again

// The cables it can grab: where each hangs and how long it is.
const cables = map.objects("cable");
let swing: Swing | null = null;
let holding: i32 = -1;       // which cable (-1: none)
let sinceLetGo: f32 = 99;    // so it doesn't grab the cable it just left
let facing: f32 = 1;
// Before this frame's physics: what stomps are judged on (a bounce mustn't turn the next contact into a hit).
let fallingBefore = false, feetBefore: f32 = 0;
GameState.setNumber("holding", 0);  // a restarted scene's cable hangs free
startAtCheckpoint();  // the start, or (a deep link: session "checkpoint") a later one

export function onUpdate(dt: f32): void {
  sincePressed = Input.pressed("jump") ? 0 : sincePressed + dt;
  safeFor = Mathf.max(0, safeFor - dt);
  me.sprite.alpha = safeFor > 0 && <i32>(safeFor * 10) % 2 == 0 ? 0.3 : 1;
  for (let i = <i32>GameState.getNumber("checkpoint"); i < checkpoints.length; i++)
    if (me.transform.x > checkpoints[i].x) GameState.setNumber("checkpoint", i + 1);
  if (holding >= 0) hang(dt);
  else move(dt);
  me.transform.setScale(48 * facing, 48);
  if (me.transform.y < -120) respawn();
  const goal = map.object("goal");
  if (goal !== null && me.transform.x > goal.x) GameState.setNumber("reachedGoal", 1);
  camera.follow(me, dt);
  fallingBefore = holding < 0 && me.velocity.y < 0;
  feetBefore = me.transform.y - FEET;
}

function face(vx: f32): void {
  if (vx < -1) facing = -1;
  else if (vx > 1) facing = 1;
}

function move(dt: f32): void {
  const v = me.velocity;
  sinceGround = v.onGround ? 0 : sinceGround + dt;
  sinceLetGo += dt;
  v.x = run(v.x, Input.axis("left", "right"), v.onGround, dt);
  v.dropThrough = Input.down("down") && Input.down("jump");
  if (v.dropThrough) {
    sincePressed = sinceGround = 99;  // the press drops; it isn't a jump waiting to fire
  } else if (jumps(sincePressed, sinceGround)) {
    v.y = JUMP + v.supportVelocityY;
    if (!Input.down("jump")) v.y *= CUT;  // pressed and let go before landing: a hop
    sincePressed = sinceGround = 99;
  } else if (Input.released("jump") && v.y > 0 && sinceLetGo > 0.2) {
    v.y *= CUT;  // a tap is a hop (not the hop off a cable)
  }
  face(v.x);
  me.sprite.play(pose(v.x, v.y, v.onGround));
  if (!v.onGround && sinceLetGo > 0.3) grabNearCable();
}

// In the air near a cable's end: take hold, keeping the jump's momentum.
function grabNearCable(): void {
  for (let i = 0; i < cables.length; i++) {
    const length = <f32>cables[i].properties.get("length").number(200);
    const dx = me.transform.x - cables[i].x, dy = me.transform.y - (cables[i].y - length);
    if (dx * dx + dy * dy > 40 * 40) continue;
    const s = new Swing(cables[i].x, cables[i].y, -GRAVITY);
    s.attach(me.transform.x, me.transform.y, me.velocity.x, me.velocity.y);
    s.length = length;  // grabbed a little off the tip: still the cable's own length
    swing = s;
    holding = i;
    GameState.setNumber("holding", i + 1);
    me.velocity.set(0, 0);
    me.velocity.setAcceleration(0, 0);
    me.sprite.play("jump");
    return;
  }
}

// Swinging: the arrows pump it; jump lets go, flying off with a little hop.
function hang(dt: f32): void {
  const s = swing!;
  if (Input.pressed("jump")) {
    me.velocity.set(s.velocityX, s.velocityY + 300);
    me.velocity.setAcceleration(0, GRAVITY);
    letGo();
    return;
  }
  s.tick(dt, Input.axis("left", "right") * 3);
  const vx = s.velocityX, vy = s.velocityY;
  const hit = me.move(s.x - me.transform.x, s.y - me.transform.y);
  if (hit.any) s.attach(me.transform.x, me.transform.y, hit.hitX != 0 ? 0 : vx, hit.hitY != 0 ? 0 : vy);
  face(vx);  // the velocity stays zero: only the swing moves it
}

function letGo(): void {
  holding = -1;
  swing = null;
  sinceLetGo = 0;
  sincePressed = 99;
  GameState.setNumber("holding", 0);
}

// Sentries: landing from above disables one and bounces Kage; else, back to the start.
export function onCollide(other: Entity): void {
  if (!other.hasTag("enemy")) return;
  if (stomps(fallingBefore, feetBefore, other.transform.y + SENTRY_TOP)) {
    spawn("puff", other.transform.x, other.transform.y);
    other.destroy();
    me.velocity.y = 600;
    GameState.add("squashed", 1);
  } else if (safeFor <= 0) {
    respawn();
    safeFor = 1.5;
  }
}

function respawn(): void {
  startAtCheckpoint();
  GameState.add("falls", 1);
}

// Back at the last checkpoint passed (or the start), standing still.
function startAtCheckpoint(): void {
  if (holding >= 0) letGo();
  const passed = <i32>GameState.getNumber("checkpoint");
  const x = passed > 0 && passed <= checkpoints.length ? checkpoints[passed - 1].x : spawnX;
  const y = passed > 0 && passed <= checkpoints.length ? checkpoints[passed - 1].y + FEET : spawnY;
  me.velocity.setAcceleration(0, GRAVITY);
  me.transform.setPosition(x, y);
  me.velocity.set(0, 0);
  sincePressed = sinceGround = 99;  // a jump pressed on the way down isn't waiting at the spawn
  camera.jumpTo(x, y);
}
