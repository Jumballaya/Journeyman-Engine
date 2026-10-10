# What the engine lacked (Neon Vow)

Written as the demo was built; each entry says how it was resolved.

- **AssemblyScript doesn't narrow module-level nullables.** `const spawn = map.object("spawn")` at the top of a script, then `if (spawn !== null) spawn.x` fails to compile; `spawn!.x` works. A language rule, not the engine's: noted for AGENTS.md's gotchas.
- **A collider's layer was also what ground held.** Sentries on their own layer (`layerMask: 4`, to tell them apart) fell straight through the level's ground, then on layer 1 only, and so never met Kage: no error, just enemies gone. Fixed in the engine: ground now holds every layer unless a `TerrainComponent` narrows its `layerMask`. The sentries are told apart by their tag.
