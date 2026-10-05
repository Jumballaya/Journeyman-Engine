# Tetris

Guideline-style Tetris built on Journeyman without engine changes: 7-bag,
SRS rotation with wall kicks, hold, ghost piece, lock delay, level speed
curve, line-clear flash, high score, pause and game over.

```sh
cd assets/scripts && npm install && cd ../..   # once
jm build && jm run
jm test                                         # rule tests (tests/game.spec.ts)
python3 tools/gen_assets.py                     # regenerate art and sounds
```

**Controls:** arrows move, Up / X rotate, Z rotate back, Down soft drop,
Space hard drop, Shift / C hold, Esc pause, F11 fullscreen. Gamepads work too.

| File | Role |
|---|---|
| `assets/scripts/lib/game.ts` | the rules: no input, sound or drawing |
| `assets/scripts/lib/pieces.ts` | shapes, colors, SRS kick tables |
| `assets/scripts/lib/well.ts` | draws the board as 200 block sprites laid over `#well-cells` in the HTML |
| `assets/scripts/lib/panel.ts` | hold/next previews and readouts in the HTML side panel |
| `assets/scripts/tetris.ts` | input → rules → sound and drawing; pause, game over |
| `assets/scripts/title.ts` | title menu and starting level |
| `scenes/puzzle_*.scene.json` | preset boards (`rows`, `pieces` params) for testing by eye: `JM_ENTRY_SCENE=scenes/puzzle_tetris.scene.json` |

See [GAPS.md](GAPS.md) for what the engine was missing.
