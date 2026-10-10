# Earthworm Donkey Country

A jungle platformer in the spirit of a certain barrel-throwing ape, starring
an earthworm with a donkey's head. Built agent-first: everything here was
made with `jm`, the docs and the files (no editor), and checked by driving the
game headless.

```sh
python3 tools/gen_art.py   # regenerate the art and levels
jm build && jm run
jm test                    # movement rules
```

**Controls:** arrows run, Space / Z jump (tap for a hop, hold for full height;
a jump pressed just before landing, or just after running off a ledge, still
counts), Down + Jump drops through a platform.

## How it's made

| File | Role |
|---|---|
| `tools/gen_art.py` | each level's ground as lines, written once: into its Tiled map (`ground`, `platform` objects) and painted as its art (dirt, grass, planks), plus the sky, parallax hills and jungle, and the donkey's frames |
| `assets/maps/level1.tmj` | level 1: image layers for the art, the drawn ground, `spawn` and `goal` points |
| `assets/scripts/donkey.ts` | the player: walk-motion velocity, jumps with coyote time and buffering, a following camera, falling into a pit respawns |
| `assets/scripts/lib/moves.ts` | the movement rules, pure, so `tests/moves.spec.ts` checks them |
