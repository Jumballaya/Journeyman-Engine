// Compiles battle.spec.ts with the project's AssemblyScript and runs each export
// against an in-memory GameState/Save.
import { test } from 'node:test';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const scripts = resolve(import.meta.dirname, '../assets/scripts');
const asc = await import(pathToFileURL(join(scripts, 'node_modules/assemblyscript/dist/asc.js')));
const dir = await mkdtemp(join(tmpdir(), 'jrpg-tests-'));
const out = join(dir, 'spec.wasm');
process.chdir(scripts);  // asc resolves @jm/runtime from the working directory's node_modules
const result = await asc.main([join(import.meta.dirname, 'battle.spec.ts'), '--debug', '--outFile', out]);
if (result.error) throw new Error(result.stderr.toString());
const module = await WebAssembly.compile(await readFile(out));
await rm(dir, { recursive: true });

// The engine's GameState (store 0) and Save (store 1), over wasm memory.
function hostState(memory) {
  const stores = [new Map(), new Map()];
  const str = (p, n) => Buffer.from(memory().buffer, p, n).toString('utf8');
  const write = (s, outPtr, cap) => {
    const bytes = Buffer.from(s, 'utf8');
    bytes.copy(Buffer.from(memory().buffer, outPtr, cap), 0, 0, Math.min(cap, bytes.length));
    return bytes.length;
  };
  return {
    __jmStateGetNumber: (s, p, n, fallback) => { const v = stores[s].get(str(p, n)); return typeof v === 'number' ? v : fallback; },
    __jmStateSetNumber: (s, p, n, v) => { stores[s].set(str(p, n), v); },
    __jmStateGetString: (s, p, n, o, cap) => { const v = stores[s].get(str(p, n)); return typeof v === 'string' ? write(v, o, cap) : -1; },
    __jmStateSetString: (s, p, n, vp, vn) => { stores[s].set(str(p, n), str(vp, vn)); },
    __jmStateHas: (s, p, n) => stores[s].has(str(p, n)) ? 1 : 0,
    __jmStateRemove: (s, p, n) => { stores[s].delete(str(p, n)); },
    __jmStateClear: (s) => { stores[s].clear(); },
  };
}

for (const { name, kind } of WebAssembly.Module.exports(module)) {
  if (kind !== 'function') continue;
  test(name, async () => {
    let instance;
    const state = hostState(() => instance.exports.memory);
    const env = { ...state, abort: () => { throw new Error(`${name}: assertion failed`); }, seed: () => 7 };
    instance = await WebAssembly.instantiate(module, { env: new Proxy(env, { get: (e, k) => e[k] ?? (() => 0) }) });
    instance.exports[name]();
  });
}
