// A tank. The server simulates every tank (Network authority "host") from
// its player's input: Input here reads the controlling player's keys, sent
// from their machine. Its script also runs on every client's copy
// (scripts: "everywhere"), only to color it and show it wrecked.
import {
  Entity, GameState, Input, Message, Net, Overrides, Params, Random, Timer, Vec2, World, angleDifference, angleTo,
  self, spawn,
} from "@jm/runtime";
import { FEED, FEED_COUNT, PHASE, red, green, blue, scoreKey, tankName } from "./lib/arena";

const MAX_HP = 3;
const TURN: f32 = 3.2;          // radians a second
const SPEED: f32 = 115;
const RELOAD: f32 = 0.45;
const SHELL_SPEED: f32 = 340;
const RESPAWN: f32 = 2;

const me = self();
const body = me.transform;
const bot = Params.number("bot") != 0;
const reload = new Timer();
const aim = new Vec2();

// Its state lives in entity.data, which the server mirrors to every copy.
if (me.isMine && !me.data.has("hp")) me.data.setNumber("hp", MAX_HP);

function present(): void {
  const wrecked = me.data.getBool("wrecked");
  const shade: f32 = wrecked ? 0.35 : 1;
  me.sprite.setColor(red(me.controller) * shade, green(me.controller) * shade, blue(me.controller) * shade,
                     wrecked ? 0.55 : 1);
}

function facing(): Vec2 { return aim.set(-Mathf.sin(body.rotation), Mathf.cos(body.rotation)); }

function fire(): void {
  if (!reload.ready) return;
  reload.start(RELOAD);
  const dir = facing();
  spawn("shell", body.x + dir.x * 16, body.y + dir.y * 16,
        new Overrides().velocity(dir.x * SHELL_SPEED, dir.y * SHELL_SPEED));
}

function drive(turn: f32, thrust: f32, dt: f32): void {
  body.rotation -= turn * TURN * dt;
  const dir = facing();
  me.move(dir.x * thrust * SPEED * dt, dir.y * thrust * SPEED * dt, 4);
}

// A bot (the server's rules add one when a player is alone): turns toward
// the nearest tank and shoots once it's roughly aimed.
function think(dt: f32): void {
  const tanks = World.findAll("tank");
  let target = Entity.NONE;
  let best: f32 = 1e9;
  for (let i = 0; i < tanks.length; i++) {
    const t = tanks[i];
    if (t.index == me.index || t.data.getBool("wrecked")) continue;
    const d = Mathf.hypot(t.transform.x - body.x, t.transform.y - body.y);
    if (d < best) {
      best = d;
      target = t;
    }
  }
  if (target.isNone) return drive(0.4, 0, dt);
  const want = angleTo(body.x, body.y, target.transform.x, target.transform.y) - Mathf.PI / 2;
  const off = angleDifference(body.rotation, want);
  drive(off > 0.05 ? -1 : off < -0.05 ? 1 : 0, best > 140 ? 0.7 : 0, dt);
  if (Mathf.abs(off) < 0.15 && best < 320) fire();
}

function respawn(): void {
  const points = World.findAll("spawn");
  if (points.length > 0) {
    const p = points[Random.int(0, points.length - 1)];
    body.setPosition(p.transform.x, p.transform.y);
  }
  me.data.setNumber("hp", MAX_HP);
  me.data.setBool("wrecked", false);
}

export function onUpdate(dt: f32): void {
  present();
  if (!me.isMine) return;  // a client's copy: the server says where it is
  reload.tick(dt);
  if (me.data.getBool("wrecked")) {
    const left = me.data.add("respawnIn", -dt);
    if (left <= 0) respawn();
    return;
  }
  if (GameState.getString(PHASE) == "over") return;
  if (bot) return think(dt);
  drive(Input.axis("left", "right"), Input.axis("down", "up"), dt);
  if (Input.down("fire")) fire();
}

// From a shell (server only): number is the shooter's player id.
export function onMessage(message: Message): void {
  if (message.name != "hit" || !me.isMine || me.data.getBool("wrecked")) return;
  if (me.data.add("hp", -1) > 0) return;
  me.data.setBool("wrecked", true);
  me.data.setNumber("respawnIn", RESPAWN);
  const killer = <i32>message.number;
  if (killer != me.controller) GameState.add(scoreKey(killer), 1);
  GameState.setString(FEED, tankName(killer) + " WRECKED " + tankName(me.controller));
  GameState.add(FEED_COUNT, 1);
}
