// Rule tests for lib/rules.ts: jm test.
import { BLACK, BLACK_KING, BLACK_MAN, Board, EMPTY, RED, RED_KING, RED_MAN, cell } from "../assets/scripts/lib/rules";

function empty(): Board {
  const b = new Board();
  for (let i = 0; i < 64; i++) b.cells[i] = EMPTY;
  return b;
}

export function redOpensWithSevenMoves(): void {
  const b = Board.start();
  assert(b.turn == RED);
  assert(b.legalMoves().length == 7);
  assert(b.count(RED) == 12 && b.count(BLACK) == 12);
}

export function menOnlyMoveForward(): void {
  const b = empty();
  b.cells[cell(3, 3)] = RED_MAN;
  const moves = b.legalMoves();
  assert(moves.length == 2);
  for (let i = 0; i < moves.length; i++) assert(moves[i].to / 8 == 4);
}

export function aCaptureMustBeTaken(): void {
  const b = empty();
  b.cells[cell(2, 2)] = RED_MAN;
  b.cells[cell(3, 3)] = BLACK_MAN;
  b.cells[cell(0, 6)] = RED_MAN;  // could step, but the capture comes first
  const moves = b.legalMoves();
  assert(moves.length == 1 && moves[0].captured == cell(3, 3));
  assert(!b.play(cell(0, 6), cell(1, 7)));
  assert(b.play(cell(2, 2), cell(4, 4)));
  assert(b.cells[cell(3, 3)] == EMPTY);
}

export function aDoubleJumpKeepsTheTurn(): void {
  const b = empty();
  b.cells[cell(0, 0)] = RED_MAN;
  b.cells[cell(1, 1)] = BLACK_MAN;
  b.cells[cell(3, 3)] = BLACK_MAN;
  b.cells[cell(7, 7)] = BLACK_MAN;
  assert(b.play(cell(0, 0), cell(2, 2)));
  assert(b.turn == RED && b.chain == cell(2, 2));
  assert(b.legalMoves().length == 1);
  assert(b.play(cell(2, 2), cell(4, 4)));
  assert(b.turn == BLACK && b.chain == -1);
}

export function reachingTheFarRowCrowns(): void {
  const b = empty();
  b.cells[cell(6, 2)] = RED_MAN;
  b.cells[cell(0, 0)] = BLACK_MAN;
  assert(b.play(cell(6, 2), cell(7, 3)));
  assert(b.cells[cell(7, 3)] == RED_KING);
}

export function kingsMoveBothWays(): void {
  const b = empty();
  b.cells[cell(4, 4)] = BLACK_KING;
  b.turn = BLACK;
  assert(b.legalMoves().length == 4);
}

export function takingTheLastPieceWins(): void {
  const b = empty();
  b.cells[cell(2, 2)] = RED_MAN;
  b.cells[cell(3, 3)] = BLACK_MAN;
  assert(b.play(cell(2, 2), cell(4, 4)));
  assert(b.winner == RED);
}

export function itSurvivesBeingWrittenDown(): void {
  const b = Board.start();
  assert(b.play(cell(2, 2), cell(3, 3)));
  const copy = Board.decode(b.encode());
  assert(copy.encode() == b.encode());
  assert(copy.turn == BLACK);
}
