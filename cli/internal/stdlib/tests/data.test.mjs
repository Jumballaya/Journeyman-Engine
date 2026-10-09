// Json, store lists/keys, Overrides tags, messages and data files against a
// small host double. Run from the repository root with the other runtime tests.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { asc } from './asc.mjs';

const root = resolve(import.meta.dirname, '../../../..');
const dir = await mkdtemp(join(tmpdir(), 'jm-data-tests-'));
const output = join(dir, 'tests.wasm');
const result = await asc.main([join(import.meta.dirname, 'data.spec.ts'), '--exportRuntime', '--debug', '--outFile', output]);
if (result.error) throw new Error(result.stderr.toString());
const module = await WebAssembly.compile(await readFile(output));
await rm(dir, { recursive: true });

function harness() {
  let instance;
  const state = new Map(), sent = [];
  const files = { 'enemies': '[{"name": "JELLY", "hp": 30}]' };
  const memory = () => instance.exports.memory.buffer;
  const str = (p, n) => Buffer.from(memory(), p, n).toString('utf8');
  const out = (s, p, cap) => {
    if (s === undefined) return -1;
    const bytes = Buffer.from(s, 'utf8');
    bytes.copy(Buffer.from(memory(), p, cap), 0, 0, Math.min(cap, bytes.length));
    return bytes.length;
  };
  const env = {
    abort: () => { throw new Error('assertion failed'); },
    seed: () => 1,
    __jmFieldId: () => 0,  // the entity module looks its fields up at start
    __jmStateGetJson: (s, p, n, o, cap) => out(state.get(str(p, n)), o, cap),
    __jmStateSetJson: (s, p, n, v, vn) => state.set(str(p, n), JSON.stringify(JSON.parse(str(v, vn)))),
    __jmStateKeys: (s, p, n, o, cap) => out(JSON.stringify([...state.keys()].filter(k => k.startsWith(str(p, n))).sort()), o, cap),
    __jmMessageFrom: () => (1n << 32n) | 7n,
    __jmMessageName: (o, cap) => out('talk', o, cap),
    __jmMessageText: (o, cap) => out('hello', o, cap),
    __jmMessageNumber: () => 2,
    __jmMessagePlayer: () => 3,
    __jmNetPlayers: (o, bytes) => {
      const ids = new Int32Array(memory(), o, bytes / 4);
      for (let i = 0; i < Math.min(20, ids.length); i++) ids[i] = i;
      return 20;
    },
    __jmNetPlayerName: (p, o, cap) => out(`P${p}`, o, cap),
    __jmNetInboxCount: () => 2,
    __jmNetInboxFrom: i => [-1, 2][i],
    __jmNetInboxName: (i, o, cap) => out(['match', 'ping'][i], o, cap),
    __jmNetInboxText: (i, o, cap) => out(['host|1.2.3.4:5|ANA', ''][i], o, cap),
    __jmNetInboxNumber: i => [0, 7][i],
    __jmDataRead: (p, n, o, cap) => out(files[str(p, n)], o, cap),
    __jmEntitySend: (i, g, np, nn, tp, tn, num) => sent.push([i, g, str(np, nn), str(tp, tn), num]),
  };
  for (const i of WebAssembly.Module.imports(module)) assert.ok(i.name in env, `Unhandled host import ${i.name}`);
  instance = new WebAssembly.Instance(module, { env });
  const string = ptr => {
    const length = new DataView(memory()).getUint32(ptr - 4, true);
    return Buffer.from(memory(), ptr, length).toString('utf16le');
  };
  return { run: instance.exports, string, sent };
}

test('json parses values, escapes and nesting, with fallbacks for missing paths', () => harness().run.jsonParses());
test('json rejects malformed text as a null value', () => harness().run.jsonRejectsMalformed());
test('json stringifies what it parses', () => {
  const h = harness();
  assert.deepEqual(JSON.parse(h.string(h.run.jsonRoundTrips())), { a: 3, b: ['x', 'y"z'], c: true, d: 0.25 });
});
test('stores keep lists and list keys by prefix', () => harness().run.storeLists());
test('overrides carry tags for the spawned entity', () => {
  const h = harness();
  assert.deepEqual(JSON.parse(h.string(h.run.overrideTags())), { tags: ['door', 'locked'] });
});
test('messages and data files read through the host', () => harness().run.messageAndData());
test('Net lists players and reads messages through the host', () => harness().run.netPlayersAndMessages());
test('entity.send addresses the receiver', () => {
  const h = harness(); h.run.entityMailbox();
  assert.deepEqual(h.sent, [[3, 1, 'hit', '', 4]]);
});
