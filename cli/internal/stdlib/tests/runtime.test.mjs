// Run from the repository root: node --test cli/internal/stdlib/tests/*.test.mjs
// Uses the demo's installed AssemblyScript compiler, the same one as jm build.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { asc } from './asc.mjs';

const root = resolve(import.meta.dirname, '../../../..');
const dir = await mkdtemp(join(tmpdir(), 'jm-runtime-tests-'));
const output = join(dir, 'tests.wasm');
const result = await asc.main([join(import.meta.dirname, 'runtime.spec.ts'), '--exportRuntime', '--exportStart', '_start', '--debug', '--outFile', output]);
if (result.error) throw new Error(result.stderr.toString());
const module = await WebAssembly.compile(await readFile(output));
await rm(dir, { recursive: true });

function harness(stores = new Map()) {
  let instance, fullscreen = false;
  const volumes = [], effects = [], enabled = [], uniforms = [], transitions = [];
  const spawns = [], classes = [], styles = [], fields = new Map(), values = new Map();
  const utf8 = (ptr, length) => Buffer.from(instance.exports.memory.buffer, ptr, length).toString('utf8');
  const key = (store, ptr, length) => `${store}:${utf8(ptr, length)}`;
  const env = {
    abort: () => { throw new Error('AssemblyScript assertion failed'); },
    seed: () => 42,
    __jmSoundPlay: () => { throw new Error("Unexpected sound playback"); },
    __jmFieldId: (c, cn, f, fn) => {
      const name = `${utf8(c, cn)}.${utf8(f, fn)}`;
      if (!fields.has(name)) fields.set(name, fields.size + 1);
      return fields.get(name);
    },
    __jmFieldGet: (index, generation, field) => values.get(`${index}:${generation}:${field}`) ?? 0,
    __jmFieldSet: (index, generation, field, value) => values.set(`${index}:${generation}:${field}`, value),
    __jmEntityIsAlive: (index) => index !== -1,
    __jmEntityHasComponent: () => 1,
    __jmWorldSpawn: (p, pn, x, y, o, on) => {
      spawns.push({ prefab: utf8(p, pn), x, y, overrides: JSON.parse(utf8(o, on) || '{}') });
      return BigInt(spawns.length);
    },
    __jmUISetClass: (p, n, c, cn, on) => { classes.push([utf8(p, n), utf8(c, cn), !!on]); return 1; },
    __jmUISetStyle: (p, n, k, kn, v, vn) => { styles.push([utf8(p, n), utf8(k, kn), utf8(v, vn)]); return 1; },
    __jmStateGetNumber: (s, p, n, fallback) => stores.get(key(s, p, n)) ?? fallback,
    __jmStateSetNumber: (s, p, n, value) => stores.set(key(s, p, n), value),
    __jmStateHas: (s, p, n) => stores.has(key(s, p, n)),
    __jmStateRemove: (s, p, n) => stores.delete(key(s, p, n)),
    __jmActionState: () => 0,
    __jmAudioSetBusVolume: (bus, value) => volumes.push([bus, value]),
    __jmWindowIsFullscreen: () => fullscreen,
    __jmWindowSetFullscreen: value => { fullscreen = !!value; },
    __jmEffectAddCustom: (p, n) => { effects.push(utf8(p, n)); return effects.length; },
    __jmEffectSetEnabled: (id, on) => enabled.push([id, !!on]),
    __jmEffectSetUniform: (id, p, n, count, x) => uniforms.push([id, utf8(p, n), x]),
    __jmSceneIsTransitioning: () => transitions.length > 0,
    __jmSceneTransition: (p, n, seconds, sh, sn) => transitions.push([utf8(p, n), seconds, utf8(sh, sn)]),
    __jmActionValue: (p, n) => ['right', 'up'].includes(utf8(p, n)) ? 1 : 0,
  };
  for (const i of WebAssembly.Module.imports(module)) assert.ok(i.name in env, `Unhandled host import ${i.name}`);
  instance = new WebAssembly.Instance(module, { env });
  instance.exports._start();
  const string = ptr => {
    const memory = instance.exports.memory.buffer;
    const length = new DataView(memory).getUint32(ptr - 4, true);
    return Buffer.from(memory, ptr, length).toString('utf16le');
  };
  return { run: instance.exports, spawns, classes, styles, fields, values, string, volumes, effects, enabled, uniforms, transitions };
}

for (const name of ['math', 'timers', 'timelines', 'health', 'menus', 'input', 'hitHistory', 'sessions', 'tweens', 'cameraFollow', 'paths', 'swings', 'pathEdges', 'floors', 'collisionLayers']) {
  test(name, () => harness().run[name]());
}
test('overrides serialize strings, overwrite fields and preserve sibling properties', () => {
  const h = harness(); const value = JSON.parse(h.string(h.run.overrides()));
  assert.deepEqual(value.VelocityComponent.velocity, [3, 4]);
  assert.deepEqual(value.TransformComponent, { scale: [5, 6], rotation: 0.5 });
  assert.equal(value.SpriteComponent.texture, 'a\\b"c\n\t\0');
  assert.deepEqual(value.ScriptComponent, { script: 'test.ts', params: { hp: 9, 'name\n': '雪\r\b\f' } });
  assert.throws(() => h.run.invalidNumber());
});
test('checkpoint survives a fresh script instance', () => {
  const stores = new Map();
  harness(stores).run.snapshotCapture();
  harness(stores).run.snapshotRestore();
});
test('projectile geometry includes centered single fans and rings without a duplicate endpoint', () => {
  const h = harness(); h.run.projectiles(); assert.equal(h.spawns.length, 8);
  const close = (a, b) => assert.ok(Math.abs(a - b) < 0.0001, `${a} != ${b}`);
  const velocity = i => h.spawns[i].overrides.VelocityComponent.velocity;
  close(velocity(0)[0], 0); close(velocity(0)[1], 100);
  close(h.spawns[0].overrides.TransformComponent.rotation, 0);
  close(velocity(1)[0], 100); close(velocity(3)[0], -100);
  close(velocity(4)[0], 100); close(velocity(5)[1], 100); close(velocity(6)[0], -100); close(velocity(7)[1], -100);
});
test('tile grid lays out centers and retains scrolling overrides', () => {
  const h = harness(); h.run.tiles();
  assert.deepEqual(h.spawns.map(s => [s.x, s.y]), [[-10, -20], [10, -20], [-10, 20], [10, 20]]);
  assert.ok(h.spawns.every(s => s.overrides.ScrollWrapComponent.maxY === 40));
});
test('UI helpers clamp fills and honor hidden classes', () => {
  const h = harness(); h.run.ui();
  assert.deepEqual(h.styles, [['bar', 'width', '100.0%'], ['empty', 'width', '0.0%'], ['panel', 'opacity', '0.5']]);
  assert.deepEqual(h.classes, [['life1', 'hidden', false], ['life2', 'hidden', false], ['life3', 'hidden', true], ['dialog', 'hidden', false]]);
});
test('follower applies offsets while preserving unrelated transform fields', () => {
  const h = harness();
  const bits = value => new Uint32Array(new Float32Array([value]).buffer)[0];
  h.values.set(`1:0:${h.fields.get('TransformComponent.x')}`, bits(12));
  h.values.set(`1:0:${h.fields.get('TransformComponent.y')}`, bits(30));
  h.run.follow();
  assert.equal(h.values.get(`2:0:${h.fields.get('TransformComponent.x')}`), bits(22));
  assert.equal(h.values.get(`2:0:${h.fields.get('TransformComponent.y')}`), bits(10));
  assert.equal(h.values.has(`2:0:${h.fields.get('TransformComponent.z')}`), false);
});

test('consuming a timeline locks its authored event order until reset', () => {
  assert.throws(() => harness().run.timelineMutationAfterTake());
});

test('session checkpoints retain defaults, flags and first-attempt values across scripts', () => {
  const stores = new Map();
  harness(stores).run.sessionCapture();
  harness(stores).run.sessionRestore();
});
test('settings persist, apply effects live, and screens own panels and transition defaults', () => {
  const stores = new Map();
  const h = harness(stores); h.run.settingsAndScreens();
  assert.deepEqual(h.effects, ['crt']);
  assert.deepEqual(h.enabled.at(-1), [1, false]);
  assert.deepEqual(h.uniforms[0], [1, 'u_strength', 1]);
  assert.ok(Math.abs(h.volumes.at(-2)[1] - 0.7) < 0.00001);
  assert.deepEqual(h.volumes.at(-1), [2, 1]);
  assert.deepEqual(h.classes, [['main', 'hidden', true], ['options', 'hidden', false], ['main', 'hidden', true], ['options', 'hidden', true]]);
  assert.deepEqual(h.transitions, [['level1', 1, 'wipe']]);
  harness(stores).run.restoreSettings();
});

test('sprite shadows configure the owner without spawning companion entities', () => {
  const h = harness(); h.run.shadows();
  const value = (entity, name) => new Float32Array(new Uint32Array([h.values.get(`${entity}:0:${h.fields.get(`SpriteComponent.${name}`)}`)]).buffer)[0];
  assert.deepEqual(h.spawns, []);
  assert.equal(value(3, 'shadowX'), 16); assert.equal(value(3, 'shadowY'), -24);
  assert.equal(value(3, 'shadowScale'), 0.75); assert.equal(value(3, 'shadowLayer'), 2);
  assert.ok(Math.abs(value(3, 'shadowAlpha') - 0.32) < 0.00001);
  assert.equal(value(4, 'shadowAlpha'), 0);
  assert.ok(Number.isNaN(value(4, 'shadowLayer')));
});
