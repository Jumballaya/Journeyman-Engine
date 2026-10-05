// jm test's runner: compiles each spec (JM_TEST_SPECS, a JSON list of paths)
// with the project's AssemblyScript and runs every exported function as a test,
// against an in-memory GameState/Save and the project's data files
// (JM_TEST_ROOT). Other host functions do nothing.
import { test } from 'node:test';
import { readFileSync, readdirSync } from 'node:fs';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { basename, join } from 'node:path';
import { pathToFileURL } from 'node:url';

const asc = await import(pathToFileURL(join(process.cwd(), 'node_modules/assemblyscript/dist/asc.js')));
const specs = JSON.parse(process.env.JM_TEST_SPECS);
const dir = await mkdtemp(join(tmpdir(), 'jm-test-'));
const root = process.env.JM_TEST_ROOT;

// Data.json/Data.text: a project path, or a short name found under assets/ ("bestiary").
function dataFile(path) {
  try {
    if (path.includes('/')) return readFileSync(join(root, path), 'utf8');
    const wanted = path.endsWith('.json') ? path : path + '.json';
    const find = d => {
      for (const e of readdirSync(d, { withFileTypes: true })) {
        if (e.isDirectory() && e.name !== 'node_modules') {
          const hit = find(join(d, e.name));
          if (hit) return hit;
        } else if (e.name === wanted) return join(d, e.name);
      }
    };
    const hit = find(join(root, 'assets'));
    return hit ? readFileSync(hit, 'utf8') : undefined;
  } catch {
    return undefined;
  }
}

function host(memory, name) {
  const stores = [new Map(), new Map()];
  const bytes = (p, n) => Buffer.from(memory().buffer, p, n);
  const str = (p, n) => bytes(p, n).toString('utf8');
  const asString = p => {  // an AssemblyScript string object (UTF-16)
    if (!p) return '';
    const n = new DataView(memory().buffer).getUint32(p - 4, true);
    return bytes(p, n).toString('utf16le');
  };
  const write = (s, out, cap) => {
    if (s === undefined) return -1;
    const b = Buffer.from(s, 'utf8');
    b.copy(bytes(out, cap), 0, 0, Math.min(cap, b.length));
    return b.length;
  };
  const value = (s, p, n) => stores[s]?.get(str(p, n));
  return {
    abort: (msg, file, line, col) => {
      throw new Error(`${name}: ${asString(msg)} at ${asString(file)}:${line}:${col}`);
    },
    seed: () => 42,
    __jmLog: (p, n) => console.log(str(p, n)),
    __jmDataRead: (p, n, o, cap) => write(dataFile(str(p, n)), o, cap),
    __jmStateGetNumber: (s, p, n, fallback) => typeof value(s, p, n) === 'number' ? value(s, p, n) : fallback,
    __jmStateSetNumber: (s, p, n, v) => stores[s]?.set(str(p, n), v),
    __jmStateGetString: (s, p, n, o, cap) => typeof value(s, p, n) === 'string' ? write(value(s, p, n), o, cap) : -1,
    __jmStateSetString: (s, p, n, vp, vn) => stores[s]?.set(str(p, n), str(vp, vn)),
    __jmStateGetJson: (s, p, n, o, cap) => value(s, p, n) === undefined ? -1 : write(JSON.stringify(value(s, p, n)), o, cap),
    __jmStateSetJson: (s, p, n, vp, vn) => stores[s]?.set(str(p, n), JSON.parse(str(vp, vn))),
    __jmStateKeys: (s, p, n, o, cap) =>
      write(JSON.stringify([...(stores[s]?.keys() ?? [])].filter(k => k.startsWith(str(p, n))).sort()), o, cap),
    __jmStateHas: (s, p, n) => stores[s]?.has(str(p, n)) ? 1 : 0,
    __jmStateRemove: (s, p, n) => stores[s]?.delete(str(p, n)),
    __jmStateClear: s => stores[s]?.clear(),
  };
}

for (const spec of specs) {
  const out = join(dir, basename(spec) + '.wasm');
  // The start function (top-level code) runs once memory is reachable, not
  // during instantiation, so it can read data files.
  const result = await asc.main([spec, '--debug', '--exportStart', '_start', '--outFile', out]);
  if (result.error) {
    test(basename(spec), () => { throw new Error(result.stderr.toString()); });
    continue;
  }
  const module = await WebAssembly.compile(await readFile(out));
  for (const { name, kind } of WebAssembly.Module.exports(module)) {
    if (kind !== 'function' || name === '_start') continue;
    test(`${basename(spec, '.spec.ts')}: ${name}`, async () => {
      let instance;
      const env = host(() => instance.exports.memory, name);
      instance = await WebAssembly.instantiate(module, { env: new Proxy(env, { get: (e, k) => e[k] ?? (() => 0) }) });
      instance.exports._start();
      instance.exports[name]();
    });
  }
}
process.on('exit', () => rm(dir, { recursive: true, force: true }));
