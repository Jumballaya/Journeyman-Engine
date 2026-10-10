# What the engine lacked (Earthworm Donkey Country)

Written as the demo was built; each entry says how it was resolved.

- **AssemblyScript doesn't narrow module-level nullables.** `const spawn = map.object("spawn")` at the top of a script, then `if (spawn !== null) spawn.x` fails to compile; `spawn!.x` works. A language rule, not the engine's: noted for AGENTS.md's gotchas.
- **A collider's layer is also what ground holds it.** Gnawbles on their own layer (`layerMask: 4`, to tell them apart) fell straight through the level's ground, which is on layer 1, and so never met the donkey: no error, just enemies gone. Tags tell them apart instead. The docs say ground takes "bodies on its layers"; worth a louder line where layers are introduced.
