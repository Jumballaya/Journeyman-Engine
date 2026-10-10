#!/usr/bin/env node
// Checks the script runtime's host imports (cli/internal/stdlib/runtime/env.ts)
// against what the engine binds: every declared function must exist with the
// same wasm signature, and every bound function must be declared. Also checks
// every component field the runtime reads (new Field("C", "f")) is a script
// field the engine registers. A mismatch would otherwise surface only at runtime.
//
//   node scripts/check-host-api.mjs <journeyman_engine> [env.ts]
import { execFileSync } from "node:child_process";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const [engine, envPath = join(dirname(fileURLToPath(import.meta.url)), "../cli/internal/stdlib/runtime/env.ts")] =
  process.argv.slice(2);
if (!engine) {
  console.error("usage: check-host-api.mjs <journeyman_engine> [env.ts]");
  process.exit(2);
}

const schema = JSON.parse(execFileSync(engine, ["--schema"], { encoding: "utf8" }));
const bound = schema.hostFunctions ?? {};

// AssemblyScript types as wasm value types (HostBinding.hpp's letters).
const letter = { i32: "i", u32: "i", bool: "i", usize: "i", isize: "i", i8: "i", u8: "i", i16: "i", u16: "i",
                 f32: "f", f64: "F", i64: "I", u64: "I", void: "v" };
const declared = {};
const problems = [];
const source = readFileSync(envPath, "utf8");
for (const m of source.matchAll(/export\s+declare\s+function\s+(\w+)\s*\(([^)]*)\)\s*:\s*(\w+)/g)) {
  const [, name, params, ret] = m;
  const types = params.split(",").map((p) => p.trim()).filter(Boolean).map((p) => p.split(":")[1]?.trim());
  const unknown = [ret, ...types].filter((t) => !(t in letter));
  if (unknown.length) {
    problems.push(`${name}: env.ts uses ${unknown.join(", ")}, which has no wasm mapping here`);
    continue;
  }
  declared[name] = `${letter[ret]}(${types.map((t) => letter[t]).join("")})`;
}

for (const [name, sig] of Object.entries(declared)) {
  if (!(name in bound)) problems.push(`${name}: declared in env.ts, but the engine binds no such function`);
  else if (bound[name] !== sig) problems.push(`${name}: env.ts says ${sig}, the engine binds ${bound[name]}`);
}
// Imports the AssemblyScript compiler adds itself (assert, Math.random, trace),
// not declared in env.ts.
const compilerImports = new Set(["abort", "seed", "trace"]);
for (const name of Object.keys(bound)) {
  if (!(name in declared) && !compilerImports.has(name)) problems.push(`${name}: bound by the engine (${bound[name]}), but not declared in env.ts`);
}

const runtimeDir = dirname(envPath);
let fieldCount = 0;
for (const file of readdirSync(runtimeDir).filter((f) => f.endsWith(".ts"))) {
  const text = readFileSync(join(runtimeDir, file), "utf8").replace(/\/\/.*$/gm, "");  // not examples in comments
  for (const [, component, field] of text.matchAll(/new Field\(\s*"(\w+)"\s*,\s*"(\w+)"\s*\)/g)) {
    fieldCount++;
    const fields = schema.components?.[component]?.scriptFields?.map((f) => f.name) ?? [];
    if (!fields.includes(field)) problems.push(`${file}: ${component}.${field} isn't a script field the engine registers`);
  }
}

if (problems.length) {
  for (const p of problems.sort()) console.error(p);
  process.exit(1);
}
console.log(`host API: ${Object.keys(declared).length} functions, ${fieldCount} fields; the runtime matches the engine`);
