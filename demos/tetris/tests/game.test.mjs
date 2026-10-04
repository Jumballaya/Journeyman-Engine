// Compiles game.spec.ts with the project's AssemblyScript and runs each export.
import { test } from 'node:test';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const scripts = resolve(import.meta.dirname, '../assets/scripts');
const asc = await import(pathToFileURL(join(scripts, 'node_modules/assemblyscript/dist/asc.js')));
const dir = await mkdtemp(join(tmpdir(), 'tetris-tests-'));
const out = join(dir, 'spec.wasm');
process.chdir(scripts);  // asc resolves @jm/runtime from the working directory's node_modules
const result = await asc.main([join(import.meta.dirname, 'game.spec.ts'), '--debug', '--outFile', out]);
if (result.error) throw new Error(result.stderr.toString());
const module = await WebAssembly.compile(await readFile(out));
await rm(dir, { recursive: true });

for (const { name, kind } of WebAssembly.Module.exports(module)) {
  if (kind !== 'function') continue;
  test(name, async () => {
    const { exports } = await WebAssembly.instantiate(module, {
      // Importing @jm/runtime links engine host functions too; stub them all.
      env: new Proxy({ abort: () => { throw new Error(`${name}: assertion failed`); }, seed: () => 42 },
                     { get: (env, key) => env[key] ?? (() => 0) }),
    });
    exports[name]();
  });
}
