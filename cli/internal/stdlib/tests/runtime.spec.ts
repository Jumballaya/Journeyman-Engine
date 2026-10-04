import { Vec2, Rect, PI, TAU, angleDifference, turnTowards, lerp, blink, fadeOut } from "../runtime/math";
import { Random } from "../runtime/random";
import { formatNumber } from "../runtime/format";
import { Timer, Timeline, Pulse } from "../runtime/timing";
import { Health } from "../runtime/health";
import { Overrides } from "../runtime/world";
import { Menu } from "../runtime/menu";
import { GameState, NumberSnapshot } from "../runtime/state";
import { Projectile } from "../runtime/projectile";
import { Entity } from "../runtime/entity";
import { TransformFollower, HitHistory } from "../runtime/follow";
import { tileGrid } from "../runtime/tiles";
import { Input } from "../runtime/input";
import { UI } from "../runtime/ui";

function near(a: f32, b: f32): void { assert(Mathf.abs(a - b) < 0.0001); }

export function math(): void {
  const v = new Vec2(0.3, 0.4).limit();
  near(v.x, 0.3); near(v.y, 0.4);
  near(v.set(1, 1).limit().length, 1);
  near(v.set(0, 0).limit().length, 0);
  const r = new Rect(-2, -3, 2, 3);
  assert(r.contains(-2, 3)); assert(!r.contains(0, 4)); near(r.clampY(-8), -3);
  near(lerp(1, 3, 2), 3); near(lerp(1, 3, -1), 1);
  near(angleDifference(PI - 0.1, -PI + 0.1), 0.2);
  near(turnTowards(PI - 0.1, -PI + 0.1, 0.05), PI - 0.05);
  near(angleDifference(0, 10 * TAU + 0.25), 0.25);
  near(fadeOut(1, 2, 1), 1); near(fadeOut(2.5, 2, 1), 0.5); near(fadeOut(4, 2, 1), 0);
  near(fadeOut(2, 2, 0), 0);
  near(blink(0, 2, 1, 0.4), 1); near(blink(0.5, 2, 1, 0.4), 0.4);
  assert(formatNumber(-4, 3) == "000"); assert(formatNumber(12.9, 4) == "0012");
  assert(formatNumber(1234, 2) == "1234");
  assert(Random.int(4, 4) == 4); assert(Random.pick<string>(["only"]) == "only");
  assert(!Random.chance(0)); assert(Random.chance(1));
}

export function timers(): void {
  const t = new Timer();
  assert(t.ready); assert(!t.tick(1));
  t.start(1); assert(!t.tick(0.5)); near(t.remaining, 0.5);
  t.extend(0.1); near(t.remaining, 0.5);
  assert(t.tick(0.7)); assert(t.ready); assert(!t.tick(1));
  t.start(0); assert(t.tick(0)); assert(!t.tick(0));
  t.start(2); t.cancel(); assert(!t.tick(10));
  t.start(1); assert(!t.tick(-10)); near(t.remaining, 1);
  const pulse = new Pulse(2); pulse.trigger(1); pulse.trigger(0.1);
  near(pulse.tick(0.25), 0.5); near(pulse.tick(1), 0);
}

export function timelines(): void {
  const t = new Timeline<i32>();
  assert(t.done);
  t.at(2, 20).at(1, 10).at(2, 21).at(0, 0);
  let e = t.take(); assert(e !== null && e.value == 0);
  assert(t.take() === null);
  t.advance(2);
  e = t.take(); assert(e !== null && e.value == 10);
  e = t.take(); assert(e !== null && e.value == 20);
  e = t.take(); assert(e !== null && e.value == 21);
  assert(t.done && t.take() === null);
  t.reset(); t.finish();
  let count = 0;
  while (t.take() !== null) count++;
  assert(count == 4 && t.done);
  t.finish(); assert(t.take() === null);
}

export function health(): void {
  const hp = new Health(10);
  assert(!hp.damage(3)); near(hp.current, 7);
  assert(!hp.damage(-4)); near(hp.current, 7);
  hp.heal(20); near(hp.current, 10);
  assert(hp.damage(50)); assert(hp.dead); near(hp.fraction, 0);
  assert(!hp.damage(1)); near(new Health(0).fraction, 0);
}

export function overrides(): string {
  const o = new Overrides().velocity(1, 2).velocity(3, 4).scale(5, 6).rotation(0.5)
    .texture("a\\b\"c\n\t\u0000").tint(1, 0.5, 0, 1).scrollY(-10, 20)
    .param("hp", 1).param("hp", 9).paramText("name\n", "雪\r\b\f")
    .text("ScriptComponent", "script", "test.ts");
  const first = o.toJson(); assert(first == o.toJson()); return first;
}

export function invalidNumber(): void { new Overrides().rotation(NaN); }

export function menus(): void {
  const empty = new Menu([]); assert(empty.index == -1); assert(empty.handle(true, false, true) == -1);
  const menu = new Menu(["a", "b", "c"]);
  assert(menu.handle(true, false, true) == 2);
  assert(menu.handle(false, true, true) == 0);
  assert(menu.handle(true, true, true) == 0);
  menu.index = 99; assert(menu.index == 2);
  menu.index = -99; assert(menu.index == 0);
}

export function snapshotCapture(): void {
  GameState.setNumber("score", 123); GameState.remove("missing");
  new NumberSnapshot(GameState, "checkpoint", ["score", "missing"]).capture();
}
export function snapshotRestore(): void {
  GameState.setNumber("score", 999); GameState.setNumber("missing", 1);
  assert(new NumberSnapshot(GameState, "checkpoint", ["score", "missing"]).restore());
  assert(GameState.getNumber("score") == 123 && !GameState.has("missing"));
  assert(!new NumberSnapshot(GameState, "absent", ["score"]).restore());
  assert(GameState.record("record", 20, 10)); assert(!GameState.record("record", 15));
  GameState.setBool("flag", true); assert(GameState.getBool("flag"));
  GameState.setNumber("flash", 0.7); assert(GameState.takeNumber("flash") == 0.7); assert(GameState.takeNumber("flash") == 0);
}

export function projectiles(): void {
  const gun = new Projectile("shot", 100, true);
  gun.fan(1, 2, PI / 2, 1, 1);
  gun.fan(1, 2, PI / 2, 0, 1);
  gun.fan(1, 2, PI / 2, 3, PI);
  gun.ring(0, 0, 4);
}

export function tiles(): void {
  tileGrid("tile", 2, 2, -10, -20, 20, 40, new Overrides().velocity(0, -40).scale(10, 20).scrollY(-40, 40));
}

export function input(): void {
  const direction = new Vec2();
  Input.vector("left", "right", "down", "up", direction);
  near(direction.length, 1);
}

export function ui(): void {
  UI.fill("bar", 2); UI.fill("empty", -1); UI.opacity("panel", 0.5);
  UI.showCount("life", 2, 3, "hidden"); UI.setVisible("dialog", true, "hidden");
}

export function follow(): void {
  const owner = new Entity(1, 0);
  const child = new Entity(2, 0);
  new TransformFollower(child, 10, -20).follow(owner.transform);
}

export function hitHistory(): void {
  const history = new HitHistory();
  assert(history.accept(new Entity(1, 0)));
  assert(!history.accept(new Entity(1, 0)));
  assert(history.accept(new Entity(1, 1)));
  assert(!history.accept(Entity.NONE));
  history.clear(); assert(history.accept(new Entity(1, 0)));
}
