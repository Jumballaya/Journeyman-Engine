# What the engine lacked (Earthworm Donkey Country)

Written as the demo was built; each entry says how it was resolved.

- **AssemblyScript doesn't narrow module-level nullables.** `const spawn = map.object("spawn")` at the top of a script, then `if (spawn !== null) spawn.x` fails to compile; `spawn!.x` works. A language rule, not the engine's: noted for AGENTS.md's gotchas.
