// The side panel in game.ui.html: hold and next-piece previews (4x2 grids of
// divs "<slot>-0".."<slot>-7") and the score readouts.
import { UI, formatNumber } from "@jm/runtime";
import { Game, PREVIEW_COUNT } from "./game";
import { Kind, cellsOf, red, green, blue } from "./pieces";

const NONE = -1;     // Game.held when empty
const UNDRAWN = -2;

export class Panel {
  private shownKinds: i32[] = [UNDRAWN, UNDRAWN, UNDRAWN, UNDRAWN];  // hold, next0..2
  private shownScore: f64 = -1;
  private shownLines: i32 = -1;

  draw(game: Game, hiscore: f64): void {
    this.preview(0, "hold", game.held);
    for (let i = 0; i < PREVIEW_COUNT; i++) this.preview(i + 1, "next" + i.toString(), game.preview(i));
    if (game.score != this.shownScore) {
      this.shownScore = game.score;
      UI.setText("score", formatNumber(game.score, 7));
      UI.setText("hiscore", formatNumber(Math.max(hiscore, game.score), 7));
    }
    if (game.lines != this.shownLines) {
      this.shownLines = game.lines;
      UI.setText("lines", game.lines.toString());
      UI.setText("level", game.level.toString());
    }
  }

  private preview(slot: i32, id: string, kind: i32): void {
    if (this.shownKinds[slot] == kind) return;
    this.shownKinds[slot] = kind;
    for (let i = 0; i < 8; i++) UI.setStyle(id + "-" + i.toString(), "background-color", "transparent");
    if (kind == NONE) return;

    // Center the spawn shape in the 4x2 grid.
    const cells = cellsOf(<Kind>kind, 0);
    let minX = 4, maxX = -1, top = -4;
    for (let i = 0; i < 8; i += 2) {
      minX = min(minX, cells[i]);
      maxX = max(maxX, cells[i]);
      top = max(top, cells[i + 1]);
    }
    const shift = (4 - (maxX - minX + 1)) / 2 - minX;
    const color = hex(<Kind>kind);
    for (let i = 0; i < 8; i += 2) {
      const col = cells[i] + shift;
      const row = top - cells[i + 1];
      UI.setStyle(id + "-" + (row * 4 + col).toString(), "background-color", color);
    }
  }
}

function hex(kind: Kind): string {
  return "#" + byte(red(kind)) + byte(green(kind)) + byte(blue(kind));
}

function byte(v: f32): string {
  const n = <i32>Mathf.round(Mathf.max(0, Mathf.min(1, v)) * 255);
  return (n < 16 ? "0" : "") + n.toString(16);
}
