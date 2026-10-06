// Rule tests for lib/game.ts; run with `node --test tests/*.test.mjs`.
import { Event, Game, Options, Phase, WIDTH } from "../assets/scripts/lib/game";
import { Kind, GARBAGE } from "../assets/scripts/lib/pieces";

function game(rows: string[], pieces: Kind[], level: i32 = 1): Game {
  const o = new Options();
  o.rows = rows;
  o.pieces = pieces;
  o.level = level;
  return new Game(o);
}

function filled(g: Game, y: i32): i32 {
  let n = 0;
  for (let x = 0; x < WIDTH; x++) if (g.cellAt(x, y) != 0) n++;
  return n;
}

export function hardDropLandsOnTheFloor(): void {
  const g = game([], [Kind.O]);
  g.hardDrop();
  assert(g.cellAt(4, 0) == Kind.O + 1 && g.cellAt(5, 1) == Kind.O + 1);
  assert(g.score == 2 * 20);  // 2 points per row dropped
}

export function verticalIClearsFourLinesAsATetris(): void {
  const row = "XXXXXXXXX.";
  const g = game([row, row, row, row], [Kind.I]);
  assert(g.rotate(1));
  for (let i = 0; i < 5; i++) g.shift(1);  // into the gap at x = 9
  g.hardDrop();
  const events = g.takeEvents();
  assert((events & Event.Tetris) != 0);
  assert(g.phase == Phase.Clearing && g.isClearingRow(0) && g.isClearingRow(3));
  g.update(1);
  assert(g.phase == Phase.Falling);
  assert(g.lines == 4 && filled(g, 0) == 0);
  assert(g.score >= 800);
}

export function scoreScalesWithLevel(): void {
  const g = game(["XXXX..XXXX"], [Kind.O], 3);
  g.hardDrop();
  g.update(1);
  assert(g.lines == 1);
  assert(g.score == 2 * 20 + 100 * 3);  // drop + single at level 3
  assert(filled(g, 0) == 2);           // the O's top half falls into row 0
}

export function wallKickRotatesAgainstTheWall(): void {
  const g = game([], [Kind.T]);
  for (let i = 0; i < 6; i++) g.shift(-1);  // flat against the left wall
  assert(g.rotate(-1));                      // needs no kick
  assert(g.rotate(-1));                      // would poke through the wall: kicked right
}

export function holdSwapsOncePerPiece(): void {
  const g = game([], [Kind.T, Kind.S, Kind.Z]);
  g.hold();
  assert(g.held == Kind.T && g.pieceKind == Kind.S);
  g.hold();  // ignored until the next piece
  assert(g.held == Kind.T && g.pieceKind == Kind.S);
  g.hardDrop();
  g.hold();
  assert(g.held == Kind.Z && g.pieceKind == Kind.T);
}

export function lockDelayWaitsHalfASecond(): void {
  const g = game([], [Kind.O, Kind.T]);
  g.update(30);                 // fall to the floor
  assert(g.pieceKind == Kind.O);
  g.update(0.3);
  assert(g.pieceKind == Kind.O);
  g.update(0.3);
  assert(g.pieceKind == Kind.T);  // locked and the next piece spawned
}

export function toppingOutEndsTheGame(): void {
  const rows = new Array<string>();
  for (let y = 0; y < 20; y++) rows.push(y % 2 == 0 ? "XXXX.XXXXX" : "XXXXX.XXXX");  // no full rows
  const g = game(rows, [Kind.O]);
  g.hardDrop();  // can't enter the field: locks in the hidden rows
  assert(g.phase == Phase.Over && (g.takeEvents() & Event.GameOver) != 0);
}

export function garbageIsDrawnGray(): void {
  const g = game(["X........."], []);
  assert(g.cellAt(0, 0) == GARBAGE + 1);
}
