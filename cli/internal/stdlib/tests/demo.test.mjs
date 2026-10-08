// Compile the actual demo scripts, then exercise their hooks through explicit
// engine doubles. This catches migrations that compile but change game rules.
import { test, after } from 'node:test';
import assert from 'node:assert/strict';
import { cp, mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { asc } from './asc.mjs';
const root = resolve(import.meta.dirname, '../../../..');
const scratch = await mkdtemp(join(tmpdir(), 'jm-demo-tests-'));
const scripts = join(scratch, 'scripts');
await cp(join(root, 'demos/strike_wing/assets/scripts'), scripts, { recursive: true, filter: p => !p.includes('node_modules') });
await cp(join(root, 'cli/internal/stdlib/runtime'), join(scripts, 'node_modules/@jm/runtime'), { recursive: true });
const entries = join(scripts, 'node_modules/.jm/entries');
await mkdir(entries, { recursive: true });
const modules = new Map();
const originalCwd = process.cwd();
process.chdir(scripts);
try {
  for (const file of await readdir(scripts)) {
    if (!file.endsWith('.ts')) continue;
    const entry = join(entries, file);
    await writeFile(entry, `import * as script from "../../../${file.slice(0, -3)}";
import { Entity } from "@jm/runtime";
export function onUpdate(dt:f32):void { if(isDefined(script.onUpdate))script.onUpdate(dt); }
export function onCollide(i:u32,g:u32):void { if(isDefined(script.onCollide))script.onCollide(new Entity(i,g)); }
`);
    const output = join(scratch, file + '.wasm');
    const result = await asc.main([entry, '--outFile', output, '--exportStart', '_start', '--exportRuntime', '--debug']);
    if (result.error) throw new Error(`${file}: ${result.stderr}`);
    modules.set(file.slice(0, -3), await WebAssembly.compile(await readFile(output)));
  }
} finally { process.chdir(originalCwd); }
after(() => rm(scratch, { recursive: true, force: true }));

const prefabs = new Map();
for (const name of await readdir(join(root, 'demos/strike_wing/assets/prefabs'))) {
  prefabs.set(name.replace('.prefab.json', ''), JSON.parse(await readFile(join(root, 'demos/strike_wing/assets/prefabs', name), 'utf8')));
}
const bits = f => new Int32Array(new Float32Array([f]).buffer)[0];
const float = b => new Float32Array(new Int32Array([b]).buffer)[0];
const packed = (i, g = 0) => BigInt(g) << 32n | BigInt(i);

function game(name, { state = {}, stores = new Map(), params = {}, x = 0, y = 0 } = {}) {
  for (const [key, value] of Object.entries(state)) stores.set(`0:${key}`, value);
  let instance, nextEntity = 1000, nextField = 1, timeScale = 1;
  let pressed = new Set(), held = new Set(), analog = new Map();
  const entities = new Map(), fieldIds = new Map(), fieldNames = new Map();
  const spawns = [], texts = new Map(), styles = new Map(), classes = new Map(), sounds = [], transitions = [];
  const utf8 = (p, n) => Buffer.from(instance.exports.memory.buffer, p, n).toString('utf8');
  const entityKey = (i, g) => `${i >>> 0}:${g >>> 0}`;
  const entity = (i, g) => entities.get(entityKey(i, g));
  const addEntity = (id, tags = [], generation = 0) => {
    const value = { alive: true, tags: new Set(tags), fields: new Map(), ready: true };
    entities.set(entityKey(id, generation), value); return value;
  };
  const owner = addEntity(1, name === 'player' ? ['player'] : ['enemy']);
  owner.fields.set('TransformComponent.x', bits(x)); owner.fields.set('TransformComponent.y', bits(y));
  owner.fields.set('TransformComponent.scaleX', bits(24));
  const setText = (value, ptr, cap) => {
    if (value === undefined) return -1;
    const bytes = Buffer.from(value);
    Buffer.from(instance.exports.memory.buffer, ptr, Math.min(cap, bytes.length)).set(bytes.subarray(0, cap));
    return bytes.length;
  };
  const liveWithTag = tag => [...entities.entries()].filter(([, e]) => e.alive && e.ready && e.tags.has(tag));
  const env = {
    abort: (message, file, line) => { throw new Error(`AssemblyScript trap in ${name} at line ${line}`); }, seed: () => 42,
    __jmSelf: () => packed(1),
    __jmFieldId: (c, cn, f, fn) => {
      const name = `${utf8(c, cn)}.${utf8(f, fn)}`;
      if (!fieldIds.has(name)) { fieldIds.set(name, nextField); fieldNames.set(nextField++, name); }
      return fieldIds.get(name);
    },
    __jmFieldGet: (i, g, field) => entity(i, g)?.fields.get(fieldNames.get(field)) ?? 0,
    __jmFieldSet: (i, g, field, value) => { const e = entity(i, g); if (e?.alive && e.ready) e.fields.set(fieldNames.get(field), value); },
    __jmEntityIsAlive: (i, g) => !!entity(i, g)?.alive,
    __jmEntityHasComponent: (i, g) => !!entity(i, g)?.ready,
    __jmEntityHasTag: (i, g, p, n) => !!entity(i, g)?.tags.has(utf8(p, n)),
    __jmWorldDestroy: (i, g) => { const e = entity(i, g); if(e)e.alive = false; },
    __jmWorldFindFirst: (p, n) => { const match = liveWithTag(utf8(p, n))[0]; return match ? packed(...match[0].split(':').map(Number)) : -1n; },
    __jmWorldFindAll: (p, n, out, bytes) => {
      const matches = liveWithTag(utf8(p, n));
      const values = new Uint32Array(instance.exports.memory.buffer, out, bytes / 4);
      for (let i = 0; i < Math.min(matches.length, values.length / 2); i++) values.set(matches[i][0].split(':').map(Number), i * 2);
      return matches.length;
    },
    __jmWorldSpawn: (p, n, x, y, o, on) => {
      const prefab = utf8(p, n), overrides = JSON.parse(utf8(o, on) || '{}'), id = nextEntity++;
      const data = prefabs.get(prefab);
      assert.ok(data, `Unknown prefab ${prefab}`);
      const e = addEntity(id, data.tags ?? []); e.ready = false;
      e.fields.set('TransformComponent.x', bits(x)); e.fields.set('TransformComponent.y', bits(y));
      spawns.push({ prefab, x, y, overrides, id }); return packed(id);
    },
    __jmStateGetNumber: (s, p, n, fallback) => stores.get(`${s}:${utf8(p, n)}`) ?? fallback,
    __jmStateSetNumber: (s, p, n, v) => stores.set(`${s}:${utf8(p, n)}`, v),
    __jmStateHas: (s, p, n) => stores.has(`${s}:${utf8(p, n)}`),
    __jmStateRemove: (s, p, n) => stores.delete(`${s}:${utf8(p, n)}`),
    __jmStateClear: s => { for(const k of stores.keys())if(k.startsWith(`${s}:`))stores.delete(k); },
    __jmParamNumber: (p, n, fallback) => params[utf8(p, n)] ?? fallback,
    __jmParamString: (p, n, out, cap) => setText(params[utf8(p, n)], out, cap),
    __jmActionState: (p, n, mode) => (mode === 1 ? pressed : held).has(utf8(p, n)),
    __jmActionValue: (p, n) => analog.get(utf8(p, n)) ?? (held.has(utf8(p, n)) ? 1 : 0),
    __jmUISetText: (p, n, v, vn) => { texts.set(utf8(p, n), utf8(v, vn)); return 1; },
    __jmUISetClass: (p, n, c, cn, on) => { classes.set(`${utf8(p, n)}:${utf8(c, cn)}`, !!on); return 1; },
    __jmUISetStyle: (p, n, k, kn, v, vn) => { styles.set(`${utf8(p, n)}:${utf8(k, kn)}`, utf8(v, vn)); return 1; },
    __jmSoundPlay: (p, n, gain, loop, bus) => { sounds.push({ name: utf8(p, n), gain, loop, bus }); return sounds.length; },
    __jmSoundFadeOut: () => {}, __jmAudioSetBusVolume: () => {},
    __jmEffectAddCustom: () => 1, __jmEffectAddBuiltin: () => 2, __jmEffectSetUniform: () => {}, __jmEffectSetEnabled: () => {},
    __jmCameraShake: () => {}, __jmRendererSetClearColor: () => {},
    __jmWindowIsFocused: () => 1, __jmWindowIsFullscreen: () => 0, __jmWindowSetFullscreen: () => {},
    __jmSceneIsTransitioning: () => transitions.length > 0,
    __jmSceneTransition: (p, n) => transitions.push(utf8(p, n)),
    __jmSceneCurrent: (out, cap) => setText('scenes/level1.scene.json', out, cap),
    __jmTimeSetScale: value => { timeScale = value; },
    __jmAppQuit: () => {},
  };
  const module = modules.get(name);
  for(const i of WebAssembly.Module.imports(module))assert.ok(i.name in env, `Missing ${name} host ${i.name}`);
  instance = new WebAssembly.Instance(module, { env }); instance.exports._start();
  const tick = (dt, actions = [], down = [], values = {}) => {
    pressed = new Set(actions); held = new Set(down); analog = new Map(Object.entries(values));
    instance.exports.onUpdate(dt);
    for (const e of entities.values()) e.ready = true;
  };
  return { tick, hit: (i, g = 0) => instance.exports.onCollide(i, g), addEntity, entities, spawns, stores, texts, styles, classes, sounds, transitions,
    get: key => stores.get(`0:${key}`), field: name => float(owner.fields.get(`TransformComponent.${name}`) ?? 0), get timeScale() { return timeScale; } };
}

test('every demo script starts and updates with valid host calls', () => {
  for (const name of modules.keys()) game(name).tick(0.016);
});
test('results skip awards bonuses exactly once and needs a second press to continue', () => {
  const g = game('stage_clear', { state: { score: 1000, clearedStage: 2, shots: 10, hits: 5, deaths: 0, kills: 8 } });
  g.tick(0.016, ['confirm']);
  assert.equal(g.get('score'), 51000); assert.deepEqual(g.transitions, []);
  assert.equal(g.texts.get('accuracy'), '50%');
  g.tick(10); assert.equal(g.get('score'), 51000);
  g.tick(0.016, ['confirm']); assert.deepEqual(g.transitions, ['boss']);
});
test('wave timeline catches up in order and does not repeat launches', () => {
  const g = game('director', { params: { stage: 1 } });
  g.tick(2.9); assert.equal(g.spawns.length, 0);
  g.tick(0.2); assert.equal(g.spawns.length, 5);
  assert.deepEqual(g.spawns.map(s => s.x), [-160, -80, 0, 80, 160]);
  g.tick(8.5); assert.equal(g.spawns.length, 12);
  assert.equal(g.spawns[5].overrides.ScriptComponent.params.pattern, 'swoop');
  g.tick(0); assert.equal(g.spawns.length, 12);
});
test('player weapon levels, bomb clearing and respawn keep their rules', () => {
  const g = game('player', { state: { power: 3, bombs: 3, lives: 2 } });
  g.tick(1.1, [], ['fire']);
  assert.equal(g.spawns.filter(s => s.prefab === 'player_bullet').length, 7);
  assert.equal(g.get('shots'), 7);
  const enemyBullet = g.addEntity(40, ['enemy_bullet']);
  g.tick(0.1, ['bomb']); assert.equal(g.get('bombs'), 2); assert.equal(enemyBullet.alive, false);
  g.tick(3);
  g.addEntity(41, ['enemy_bullet']); g.hit(41);
  assert.equal(g.get('lives'), 1); assert.equal(g.get('power'), 2); assert.equal(g.field('y'), -2000);
  g.tick(1.5); assert.equal(g.field('y'), -2000);
  g.tick(0.2); assert.equal(g.field('y'), -380);
});
test('enemy accepts each bomb source once, including recycled entity indices', () => {
  const g = game('enemy', { params: { hp: 30, score: 400 }, y: 200 });
  g.addEntity(40, ['bomb']); g.hit(40); g.hit(40);
  g.addEntity(41, ['bomb']); g.hit(41); g.hit(40);
  assert.equal(g.get('kills'), undefined);
  g.addEntity(40, ['bomb'], 1); g.hit(40, 1);
  assert.equal(g.get('kills'), 1); assert.equal(g.get('score'), 400);
});
test('boss ring/spiral phases and death sequence remain operational', () => {
  const g = game('boss', { y: 170 }); g.tick(0.01);
  for (let i = 0; i < 11; i++) { g.addEntity(40 + i, ['bomb']); g.hit(40 + i); }
  assert.ok(g.get('bossHealth') < 0.33);
  g.tick(1.3); g.tick(1.1);
  assert.ok(g.spawns.filter(s => s.prefab === 'enemy_bullet_blue').length >= 18);
  for (let i = 11; i < 15; i++) { g.addEntity(40 + i, ['bomb']); g.hit(40 + i); }
  g.tick(2.7);
  assert.equal(g.get('bossDefeated'), 1); assert.equal(g.get('score'), 50000);
});
test('pause restart restores a checkpoint made by a different script', () => {
  const stores = new Map();
  const director = game('director', { stores, state: { score: 1000, lives: 2, bombs: 3, power: 2 }, params: { stage: 1 } });
  stores.set('0:score', 8000); stores.set('0:lives', 0);
  const pause = game('pause', { stores }); pause.tick(0.016, ['pause']); assert.equal(pause.timeScale, 0);
  pause.tick(0.016, ['down']); pause.tick(0.016, ['confirm']);
  assert.equal(director.get('score'), 1000); assert.equal(director.get('lives'), 2);
  assert.deepEqual(pause.transitions, ['scenes/level1.scene.json']);
});
test('all background themes build the expected wrapping grid', () => {
  for (const theme of ['land', 'ocean', 'storm']) {
    const g = game('scroller', { params: { theme } });
    const tiles = g.spawns.filter(s => s.prefab === 'bg_tile');
    assert.equal(tiles.length, theme === 'land' ? 330 : 88);
    assert.ok(tiles.every(s => s.overrides.ScrollWrapComponent.maxY - s.overrides.ScrollWrapComponent.minY === 704));
    g.tick(1);
  }
});

test('each weapon level emits the intended volley and records its real shot count', () => {
  for (const [power, count] of [[1, 2], [2, 4], [3, 7]]) {
    const g = game('player', { state: { power, lives: 2, bombs: 3 } });
    g.tick(1.1, [], ['fire']);
    assert.equal(g.spawns.filter(s => s.prefab === 'player_bullet').length, count);
    assert.equal(g.get('shots'), count);
  }
});
