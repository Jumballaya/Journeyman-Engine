// The AssemblyScript compiler the runtime tests use: JM_ASC (an assemblyscript
// package folder; `jm doctor --json` names the one jm builds with as
// toolchain.asc), else Strike Wing's own npm install.
import { existsSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const root = resolve(import.meta.dirname, '../../../..');
const candidates = [
  process.env.JM_ASC,
  join(root, 'demos/strike_wing/assets/scripts/node_modules/assemblyscript'),
].filter(Boolean);
const found = candidates.find(dir => existsSync(join(dir, 'dist/asc.js')));
if (!found) throw new Error(`no AssemblyScript: set JM_ASC="$(cd demos/strike_wing && jm doctor --json | jq -r .toolchain.asc)" (looked in ${candidates.join(', ')})`);

export const asc = await import(pathToFileURL(join(found, 'dist/asc.js')));
