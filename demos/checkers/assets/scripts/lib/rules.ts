// Checkers (American rules) on an 8x8 board: men move one square diagonally
// forward, kings either way; a capture jumps an adjacent enemy piece to the
// empty square behind it, and must be taken when there is one, continuing
// with the same piece while it can (a man that reaches the far row is
// crowned, which ends the move). Red starts at the bottom (rows 0-2) and
// moves first; whoever can't move loses.

export const EMPTY: i32 = 0;
export const RED_MAN: i32 = 1;
export const RED_KING: i32 = 2;
export const BLACK_MAN: i32 = 3;
export const BLACK_KING: i32 = 4;

export const RED: i32 = 1;    // a side
export const BLACK: i32 = 2;

export function sideOf(piece: i32): i32 {
  return piece == RED_MAN || piece == RED_KING ? RED : piece == BLACK_MAN || piece == BLACK_KING ? BLACK : 0;
}
export function isKing(piece: i32): bool { return piece == RED_KING || piece == BLACK_KING; }
export function other(side: i32): i32 { return side == RED ? BLACK : RED; }
export function cell(row: i32, col: i32): i32 { return row * 8 + col; }
export function rowOf(index: i32): i32 { return index / 8; }
export function colOf(index: i32): i32 { return index % 8; }
// The dark squares, where pieces stand.
export function playable(row: i32, col: i32): bool { return (row + col) % 2 == 0; }

export class Move {
  constructor(readonly from: i32, readonly to: i32, readonly captured: i32 = -1) {}
}

export class Board {
  cells: StaticArray<i32> = new StaticArray<i32>(64);
  turn: i32 = RED;
  chain: i32 = -1;  // mid-capture: the piece that must jump again
  winner: i32 = 0;

  static start(): Board {
    const b = new Board();
    for (let r = 0; r < 8; r++) {
      for (let c = 0; c < 8; c++) {
        if (!playable(r, c)) continue;
        if (r < 3) b.cells[cell(r, c)] = RED_MAN;
        else if (r > 4) b.cells[cell(r, c)] = BLACK_MAN;
      }
    }
    return b;
  }

  // "<64 digits>,<turn>,<chain>,<winner>": how the game is kept in entity.data.
  encode(): string {
    let s = "";
    for (let i = 0; i < 64; i++) s += this.cells[i].toString();
    return s + "," + this.turn.toString() + "," + this.chain.toString() + "," + this.winner.toString();
  }

  static decode(text: string): Board {
    const b = new Board();
    const parts = text.split(",");
    if (parts.length != 4 || parts[0].length != 64) return Board.start();
    for (let i = 0; i < 64; i++) b.cells[i] = <i32>(parts[0].charCodeAt(i) - 48);
    b.turn = I32.parseInt(parts[1]);
    b.chain = I32.parseInt(parts[2]);
    b.winner = I32.parseInt(parts[3]);
    return b;
  }

  // Steps and jumps for the piece at `from`, ignoring whether a capture is due elsewhere.
  private movesOf(from: i32, out: Move[]): void {
    const piece = this.cells[from];
    const side = sideOf(piece);
    if (side == 0) return;
    const forward = side == RED ? 1 : -1;
    const r = rowOf(from), c = colOf(from);
    for (let dr = -1; dr <= 1; dr += 2) {
      if (!isKing(piece) && dr != forward) continue;
      for (let dc = -1; dc <= 1; dc += 2) {
        const r1 = r + dr, c1 = c + dc;
        if (r1 < 0 || r1 > 7 || c1 < 0 || c1 > 7) continue;
        const next = this.cells[cell(r1, c1)];
        if (next == EMPTY) {
          out.push(new Move(from, cell(r1, c1)));
          continue;
        }
        const r2 = r1 + dr, c2 = c1 + dc;
        if (sideOf(next) == other(side) && r2 >= 0 && r2 <= 7 && c2 >= 0 && c2 <= 7 && this.cells[cell(r2, c2)] == EMPTY) {
          out.push(new Move(from, cell(r2, c2), cell(r1, c1)));
        }
      }
    }
  }

  // Every legal move for the side to play: only captures when one exists,
  // only the chaining piece's captures mid-capture.
  legalMoves(): Move[] {
    const all = new Array<Move>();
    if (this.winner != 0) return all;
    if (this.chain >= 0) {
      this.movesOf(this.chain, all);
    } else {
      for (let i = 0; i < 64; i++) {
        if (sideOf(this.cells[i]) == this.turn) this.movesOf(i, all);
      }
    }
    const captures = all.filter((m: Move) => m.captured >= 0);
    return captures.length > 0 || this.chain >= 0 ? captures : all;
  }

  movesFrom(from: i32): Move[] {
    const legal = this.legalMoves();
    const out = new Array<Move>();
    for (let i = 0; i < legal.length; i++) if (legal[i].from == from) out.push(legal[i]);
    return out;
  }

  // Plays from -> to for the side to move; false (nothing changes) if it's not legal.
  play(from: i32, to: i32): bool {
    const legal = this.legalMoves();
    let move: Move | null = null;
    for (let i = 0; i < legal.length; i++) {
      if (legal[i].from == from && legal[i].to == to) move = legal[i];
    }
    if (move === null) return false;
    let piece = this.cells[from];
    this.cells[from] = EMPTY;
    if (move.captured >= 0) this.cells[move.captured] = EMPTY;
    const lastRow = sideOf(piece) == RED ? 7 : 0;
    const crowned = !isKing(piece) && rowOf(to) == lastRow;
    if (crowned) piece = piece == RED_MAN ? RED_KING : BLACK_KING;
    this.cells[to] = piece;

    this.chain = -1;
    if (move.captured >= 0 && !crowned) {
      const more = new Array<Move>();
      this.movesOf(to, more);
      for (let i = 0; i < more.length; i++) {
        if (more[i].captured >= 0) this.chain = to;
      }
    }
    if (this.chain < 0) {
      this.turn = other(this.turn);
      if (this.legalMoves().length == 0) this.winner = other(this.turn);
    }
    return true;
  }

  count(side: i32): i32 {
    let n = 0;
    for (let i = 0; i < 64; i++) if (sideOf(this.cells[i]) == side) n++;
    return n;
  }
}
