// The rules of Tetris (guideline-style): 7-bag, SRS rotation, hold, ghost,
// lock delay, line clears and scoring. No input, sound or drawing: scripts
// call the actions and read the state and events.
import { Random } from "@jm/runtime";
import { GARBAGE, Kind, KIND_COUNT, KICK_TESTS, cellsOf, kickX, kickY } from "./pieces";

export const WIDTH = 10;
export const VISIBLE_HEIGHT = 20;
const HEIGHT = 22;            // two hidden rows above the visible field
const SPAWN_X = 3;
const SPAWN_Y = 21;
const LOCK_DELAY: f32 = 0.5;
const MAX_LOCK_RESETS = 15;   // moves that may postpone locking on one surface
const CLEAR_SECONDS: f32 = 0.3;
export const PREVIEW_COUNT = 3;
const EMPTY: u8 = 0;

// Things that happened since the last takeEvents(); a bit set.
export enum Event {
  Moved = 1, Rotated = 2, Locked = 4, HardDropped = 8, Held = 16,
  Cleared = 32, Tetris = 64, LevelUp = 128, GameOver = 256,
}

export enum Phase { Falling, Clearing, Over }

// How a game starts. `rows` presets the board, bottom row first ("XXXXXXXXX."),
// and `pieces` come before the 7-bag; both are for puzzles, B-type and tests.
export class Options {
  level: i32 = 1;
  rows: string[] = [];
  pieces: Kind[] = [];
}

class Piece {
  rotation: i32 = 0;
  x: i32;
  y: i32 = SPAWN_Y;
  constructor(public kind: Kind) { this.x = kind == Kind.O ? SPAWN_X + 1 : SPAWN_X; }  // all spawn centered
}

export class Game {
  readonly startLevel: i32;
  score: f64 = 0;
  lines: i32 = 0;
  phase: Phase = Phase.Falling;
  held: i32 = -1;  // a Kind, or -1

  private board: Uint8Array = new Uint8Array(WIDTH * HEIGHT);  // Kind + 1 per cell, 0 = empty
  private piece: Piece = new Piece(Kind.I);  // replaced in the constructor
  private queue: Array<Kind> = new Array<Kind>();
  private canHold: bool = true;
  private fallTimer: f32 = 0;
  private lockTimer: f32 = 0;
  private lockResets: i32 = 0;
  private softDropping: bool = false;
  private clearing: Array<i32> = new Array<i32>();  // full rows, shown flashing before they vanish
  private clearTimer: f32 = 0;
  private events: u32 = 0;

  constructor(options: Options = new Options()) {
    this.startLevel = max(1, options.level);
    for (let y = 0; y < min(options.rows.length, VISIBLE_HEIGHT); y++) {
      for (let x = 0; x < min(options.rows[y].length, WIDTH); x++) {
        if (options.rows[y].charAt(x) != ".") this.board[y * WIDTH + x] = <u8>(GARBAGE + 1);
      }
    }
    for (let i = 0; i < options.pieces.length; i++) this.queue.push(options.pieces[i]);
    this.refillQueue();
    this.piece = new Piece(this.queue.shift());
  }

  get level(): i32 { return max(this.startLevel, 1 + this.lines / 10); }

  // ---- Actions -----------------------------------------------------------------

  shift(dx: i32): bool {
    if (this.phase != Phase.Falling || !this.tryMove(dx, 0, 0)) return false;
    this.events |= Event.Moved;
    return true;
  }

  // turn: +1 clockwise, -1 counter-clockwise.
  rotate(turn: i32): bool {
    if (this.phase != Phase.Falling) return false;
    const p = this.piece;
    for (let i = 0; i < KICK_TESTS; i++) {
      if (this.tryMove(kickX(p.kind, p.rotation, turn, i), kickY(p.kind, p.rotation, turn, i), turn)) {
        this.events |= Event.Rotated;
        return true;
      }
    }
    return false;
  }

  // While held, the piece falls 20x faster and scores a point per row.
  setSoftDrop(on: bool): void { this.softDropping = on; }

  hardDrop(): void {
    if (this.phase != Phase.Falling) return;
    let rows = 0;
    while (this.fits(this.piece, 0, -1, 0)) {
      this.piece.y--;
      rows++;
    }
    this.score += 2 * rows;
    this.events |= Event.HardDropped;
    this.lock();
  }

  hold(): void {
    if (this.phase != Phase.Falling || !this.canHold) return;
    const current = this.piece.kind;
    if (this.held < 0) this.spawn(this.nextKind());
    else this.spawn(<Kind>this.held);
    this.held = current;
    this.canHold = false;
    this.events |= Event.Held;
  }

  update(dt: f32): void {
    if (this.phase == Phase.Clearing) {
      this.clearTimer -= dt;
      if (this.clearTimer <= 0) this.finishClear();
    }
    if (this.phase != Phase.Falling) return;

    const interval = this.softDropping ? Mathf.min(this.gravity(), 0.05) : this.gravity();
    this.fallTimer += dt;
    let fell = false;
    while (this.fallTimer >= interval && this.fits(this.piece, 0, -1, 0)) {
      this.fallTimer -= interval;
      this.piece.y--;
      fell = true;
      if (this.softDropping) this.score += 1;
    }
    if (this.fits(this.piece, 0, -1, 0) || fell) {  // lock delay starts once it has landed
      this.lockTimer = 0;
      return;
    }
    this.fallTimer = 0;
    this.lockTimer += dt;
    if (this.lockTimer >= LOCK_DELAY) this.lock();
  }

  takeEvents(): u32 {
    const e = this.events;
    this.events = 0;
    return e;
  }

  // ---- State for drawing ---------------------------------------------------------

  // Kind + 1 at a cell of the settled board, 0 if empty.
  cellAt(x: i32, y: i32): u8 { return this.board[y * WIDTH + x]; }

  isClearingRow(y: i32): bool { return this.clearing.includes(y); }

  get pieceKind(): Kind { return this.piece.kind; }
  get pieceVisible(): bool { return this.phase == Phase.Falling; }
  // The active piece's cells: x0, y0, x1, y1, ... in board coordinates.
  pieceCells(out: i32[]): void { this.cellsAt(this.piece, 0, out); }
  // Where a hard drop would land.
  ghostCells(out: i32[]): void {
    let dy = 0;
    while (this.fits(this.piece, 0, dy - 1, 0)) dy--;
    this.cellsAt(this.piece, dy, out);
  }

  preview(index: i32): Kind { return this.queue[index]; }

  // ---- Rules -------------------------------------------------------------------------

  private gravity(): f32 {  // seconds per row (guideline curve)
    const l = <f32>(min(this.level, 20) - 1);
    return Mathf.pow(0.8 - l * 0.007, l);
  }

  private fits(p: Piece, dx: i32, dy: i32, turn: i32): bool {
    const cells = cellsOf(p.kind, p.rotation + turn);
    for (let i = 0; i < 8; i += 2) {
      const x = p.x + dx + cells[i];
      const y = p.y + dy + cells[i + 1];
      if (x < 0 || x >= WIDTH || y < 0) return false;
      if (y < HEIGHT && this.board[y * WIDTH + x] != EMPTY) return false;
    }
    return true;
  }

  private tryMove(dx: i32, dy: i32, turn: i32): bool {
    if (!this.fits(this.piece, dx, dy, turn)) return false;
    const p = this.piece;
    p.x += dx;
    p.y += dy;
    p.rotation = (p.rotation + turn) & 3;
    // Moving while resting on something postpones locking, a limited number of times.
    if (!this.fits(p, 0, -1, 0) && this.lockResets < MAX_LOCK_RESETS) {
      this.lockTimer = 0;
      this.lockResets++;
    }
    return true;
  }

  private cellsAt(p: Piece, dy: i32, out: i32[]): void {
    const cells = cellsOf(p.kind, p.rotation);
    for (let i = 0; i < 8; i += 2) {
      out[i] = p.x + cells[i];
      out[i + 1] = p.y + dy + cells[i + 1];
    }
  }

  private lock(): void {
    const cells = new Array<i32>(8);
    this.cellsAt(this.piece, 0, cells);
    let visible = false;
    for (let i = 0; i < 8; i += 2) {
      if (cells[i + 1] < HEIGHT) this.board[cells[i + 1] * WIDTH + cells[i]] = <u8>(this.piece.kind + 1);
      if (cells[i + 1] < VISIBLE_HEIGHT) visible = true;
    }
    this.events |= Event.Locked;
    if (!visible) {  // locked entirely above the field
      this.endGame();
      return;
    }
    for (let y = 0; y < HEIGHT; y++) {
      if (this.rowFull(y)) this.clearing.push(y);
    }
    if (this.clearing.length == 0) {
      this.spawn(this.nextKind());
      return;
    }
    this.phase = Phase.Clearing;
    this.clearTimer = CLEAR_SECONDS;
    this.events |= this.clearing.length == 4 ? Event.Tetris : Event.Cleared;
  }

  private finishClear(): void {
    const levelBefore = this.level;
    const count = this.clearing.length;
    this.score += <f64>([0, 100, 300, 500, 800][count] * levelBefore);
    this.lines += count;
    if (this.level > levelBefore) this.events |= Event.LevelUp;
    for (let i = count - 1; i >= 0; i--) this.removeRow(this.clearing[i]);  // top row first
    this.clearing = [];
    this.phase = Phase.Falling;
    this.spawn(this.nextKind());
  }

  private rowFull(y: i32): bool {
    for (let x = 0; x < WIDTH; x++) {
      if (this.board[y * WIDTH + x] == EMPTY) return false;
    }
    return true;
  }

  private removeRow(row: i32): void {
    this.board.copyWithin(row * WIDTH, (row + 1) * WIDTH);
    this.board.fill(EMPTY, (HEIGHT - 1) * WIDTH);
  }

  private spawn(kind: Kind): void {
    this.piece = new Piece(kind);
    this.canHold = true;
    this.fallTimer = 0;
    this.lockTimer = 0;
    this.lockResets = 0;
    if (!this.fits(this.piece, 0, 0, 0)) this.endGame();
  }

  private endGame(): void {
    this.phase = Phase.Over;
    this.events |= Event.GameOver;
  }

  private nextKind(): Kind {
    const kind = this.queue.shift();
    this.refillQueue();
    return kind;
  }

  // 7-bag: every run of seven pieces holds one of each.
  private refillQueue(): void {
    while (this.queue.length <= PREVIEW_COUNT) {
      const bag = new Array<Kind>();
      for (let k = 0; k < KIND_COUNT; k++) bag.push(<Kind>k);
      for (let i = KIND_COUNT - 1; i > 0; i--) {
        const j = Random.int(0, i);
        const t = bag[i];
        bag[i] = bag[j];
        bag[j] = t;
      }
      for (let i = 0; i < KIND_COUNT; i++) this.queue.push(bag[i]);
    }
  }
}
