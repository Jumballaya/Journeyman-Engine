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

Start at a checkpoint (to try one stretch): `echo '{"checkpoint": 1}' > cp.json && JM_SESSION=cp.json jm run`.

**Controls:** arrows run, Space / Z jump (tap for a hop, hold for full height;
a jump pressed just before landing, or just after running off a ledge, still
counts), Down + Jump drops through a platform. Touch a vine's end in the air to
grab it; the arrows pump the swing and Jump lets go. Land on a gnawble to
squash it; touching one from the side sends you back to the last checkpoint
(the banana posts), blinking safe for a moment.

## How it's made

| File | Role |
|---|---|
| `tools/gen_art.py` | each level's ground as lines, written once: into its Tiled map (`ground`, `platform` objects) and painted as its art (dirt, grass, planks), plus the sky, parallax hills and jungle, and the donkey's frames |
| `assets/maps/level1.tmj` | level 1: image layers for the art, the drawn ground, `spawn` and `goal` points |
| `assets/scripts/donkey.ts` | the player: walk-motion velocity, jumps with coyote time and buffering, a following camera, falling into a pit respawns |
| `assets/scripts/level.ts` | on the map: puts its vines and gnawbles where the map marks them |
| `assets/scripts/vine.ts`, `gnawble.ts` | a vine's rope (to whoever holds it, else swaying); a gnawble walks and turns at walls and ledges |
| `assets/scripts/lib/moves.ts` | the movement rules, pure, so `tests/moves.spec.ts` checks them |
