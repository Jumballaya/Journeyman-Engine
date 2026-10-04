// Draws a Game as a grid of block sprites, one per visible cell. Only cells
// whose color changed are written.
import { Entity, spawn } from "@jm/runtime";
import { Game, WIDTH, VISIBLE_HEIGHT } from "./game";
import { Kind, red, green, blue } from "./pieces";

// Matches the well's frame in game.ui.html.
export const CELL: f32 = 28;
const LEFT: f32 = -220;
const BOTTOM: f32 = -300;
const COUNT = WIDTH * VISIBLE_HEIGHT;

const EMPTY_SHADE: f32 = 0.16;
const EMPTY_ALPHA: f32 = 0.35;
const GHOST_ALPHA: f32 = 0.28;

export class Well {
  private cells: Array<Entity> = new Array<Entity>();
  private shown: Float32Array = new Float32Array(COUNT * 4);   // last color written per cell
  private wanted: Float32Array = new Float32Array(COUNT * 4);
  private scratch: Array<i32> = new Array<i32>(8);
  private ready: bool = false;

  constructor() {
    for (let y = 0; y < VISIBLE_HEIGHT; y++) {
      for (let x = 0; x < WIDTH; x++) {
        this.cells.push(spawn("cell", LEFT + CELL * (<f32>x + 0.5), BOTTOM + CELL * (<f32>y + 0.5)));
      }
    }
    this.shown.fill(-1);
  }

  // `flash` (0..1) whitens rows that are being cleared.
  draw(game: Game, flash: f32): void {
    // Spawned cells exist from the next frame; writes before that are lost.
    if (!this.ready && !(this.ready = this.cells[0].has("SpriteComponent"))) return;

    for (let y = 0; y < VISIBLE_HEIGHT; y++) {
      const clearing = game.isClearingRow(y);
      for (let x = 0; x < WIDTH; x++) {
        const cell = game.cellAt(x, y);
        if (cell == 0) this.want(x, y, EMPTY_SHADE, EMPTY_SHADE, EMPTY_SHADE + 0.06, EMPTY_ALPHA);
        else if (clearing) this.wantKind(x, y, <Kind>(cell - 1), 1, flash);
        else this.wantKind(x, y, <Kind>(cell - 1), 1, 0);
      }
    }
    if (game.pieceVisible) {
      game.ghostCells(this.scratch);
      this.wantPiece(game.pieceKind, GHOST_ALPHA);
      game.pieceCells(this.scratch);
      this.wantPiece(game.pieceKind, 1);
    }
    this.flush();
  }

  private wantPiece(kind: Kind, alpha: f32): void {
    for (let i = 0; i < 8; i += 2) {
      const x = this.scratch[i], y = this.scratch[i + 1];
      if (y < VISIBLE_HEIGHT) this.wantKind(x, y, kind, alpha, 0);
    }
  }

  private wantKind(x: i32, y: i32, kind: Kind, alpha: f32, white: f32): void {
    this.want(x, y, red(kind) + (1 - red(kind)) * white, green(kind) + (1 - green(kind)) * white,
              blue(kind) + (1 - blue(kind)) * white, alpha);
  }

  private want(x: i32, y: i32, r: f32, g: f32, b: f32, a: f32): void {
    const i = (y * WIDTH + x) * 4;
    this.wanted[i] = r;
    this.wanted[i + 1] = g;
    this.wanted[i + 2] = b;
    this.wanted[i + 3] = a;
  }

  private flush(): void {
    for (let c = 0; c < COUNT; c++) {
      const i = c * 4;
      if (this.wanted[i] == this.shown[i] && this.wanted[i + 1] == this.shown[i + 1] &&
          this.wanted[i + 2] == this.shown[i + 2] && this.wanted[i + 3] == this.shown[i + 3]) continue;
      this.cells[c].sprite.setColor(this.wanted[i], this.wanted[i + 1], this.wanted[i + 2], this.wanted[i + 3]);
      for (let k = 0; k < 4; k++) this.shown[i + k] = this.wanted[i + k];
    }
  }
}
