// The board, shared: the game lives in its entity.data, which the host
// keeps and mirrors to the other player. This script runs on both machines
// (Network scripts: "everywhere"): each draws the board from its side and
// sends its moves to the board, and only the host's copy (me.isMine) judges
// and plays them.
import {
  Camera, Entity, GameState, Input, Message, Net, NetStatus, Scene, Timer, UI, Vec2, self, spawn,
} from "@jm/runtime";
import {
  BLACK, Board, EMPTY, Move, RED, cell, colOf, isKing, playable, rowOf, sideOf,
} from "./lib/rules";
import { NOTE, OPPONENT } from "./lib/match";

const SQUARE: f32 = 38;
const ORIGIN_X: f32 = -110;  // the board's center
const ORIGIN_Y: f32 = -4;

const me = self();
// Host plays red; offline, one machine plays both sides (hot seat).
const mySide = !Net.online ? 0 : Net.isHost ? RED : BLACK;
const flip = mySide == BLACK;  // each player sees their own pieces at the bottom

let board = Board.start();
let seen = "";
let cursor = mySide == BLACK ? cell(5, 5) : cell(2, 2);  // on one of your own pieces
let selected = -1;
const squares = new Array<Entity>(64);
const pieces = new Array<Entity>(64);
const crowns = new Array<Entity>(64);
const pointer = new Vec2();
const world = new Vec2();
const leaving = new Timer();
let opponentGone = false;

// ---- The host keeps the game ------------------------------------------------------------

function keep(b: Board): void { me.data.setString("game", b.encode()); }
if (me.isMine && !me.data.has("game")) keep(Board.start());

export function onMessage(message: Message): void {
  if (!me.isMine) return;
  const b = Board.decode(me.data.getString("game"));
  // Whose move it is: the host plays red, the other player black.
  const side = message.player == Net.hostPlayer || !Net.online ? b.turn : BLACK;
  if (message.name == "move" && side == b.turn) {
    const parts = message.text.split(",");
    if (parts.length == 2 && b.play(I32.parseInt(parts[0]), I32.parseInt(parts[1]))) keep(b);
  } else if (message.name == "rematch" && b.winner != 0) {
    keep(Board.start());
  }
}

// ---- Drawing, from this player's side --------------------------------------------------

function screenOf(index: i32, out: Vec2): Vec2 {
  const r = flip ? 7 - rowOf(index) : rowOf(index);
  const c = flip ? 7 - colOf(index) : colOf(index);
  return out.set(ORIGIN_X + (<f32>c - 3.5) * SQUARE, ORIGIN_Y + (<f32>r - 3.5) * SQUARE);
}

function indexAt(x: f32, y: f32): i32 {
  const c = <i32>Mathf.floor((x - ORIGIN_X) / SQUARE + 4);
  const r = <i32>Mathf.floor((y - ORIGIN_Y) / SQUARE + 4);
  if (r < 0 || r > 7 || c < 0 || c > 7) return -1;
  return flip ? cell(7 - r, 7 - c) : cell(r, c);
}

const at = new Vec2();
for (let i = 0; i < 64; i++) {
  screenOf(i, at);
  squares[i] = spawn("square", at.x, at.y);
  pieces[i] = spawn("piece", at.x, at.y);
  crowns[i] = spawn("crown", at.x, at.y + 1);
}

function myTurn(): bool { return board.winner == 0 && (mySide == 0 || board.turn == mySide); }

function draw(): void {
  const targets = selected >= 0 ? board.movesFrom(selected) : new Array<Move>();
  for (let i = 0; i < 64; i++) {
    const dark = playable(rowOf(i), colOf(i));
    let r: f32 = dark ? 0.36 : 0.86, g: f32 = dark ? 0.24 : 0.78, b: f32 = dark ? 0.17 : 0.62;
    for (let t = 0; t < targets.length; t++) {
      if (targets[t].to == i) { r = 0.3; g = 0.55; b = 0.3; }
    }
    if (i == selected) { r = 0.75; g = 0.6; b = 0.2; }
    if (i == board.chain) { r = 0.8; g = 0.4; b = 0.2; }
    squares[i].sprite.setColor(r, g, b);
    if (i == cursor && myTurn()) squares[i].sprite.setColor(r + 0.15, g + 0.15, b + 0.15);

    const piece = board.cells[i];
    const side = sideOf(piece);
    pieces[i].sprite.alpha = piece == EMPTY ? 0 : 1;
    if (side == RED) pieces[i].sprite.setColor(0.86, 0.2, 0.18);
    if (side == BLACK) pieces[i].sprite.setColor(0.2, 0.2, 0.24);
    crowns[i].sprite.alpha = isKing(piece) ? 1 : 0;
  }

  const opponent = GameState.getString(OPPONENT).toUpperCase();
  UI.setText("red", "RED   " + (mySide == BLACK ? opponent : "YOU") + "   " + board.count(RED).toString());
  UI.setText("black", "BLACK " + (mySide == RED ? opponent : "YOU") + "   " + board.count(BLACK).toString());
  if (mySide == 0) {
    UI.setText("red", "RED   " + board.count(RED).toString());
    UI.setText("black", "BLACK " + board.count(BLACK).toString());
  }
  if (board.winner != 0) {
    const won = mySide == 0 || board.winner == mySide;
    UI.setText("turn", mySide == 0 ? (board.winner == RED ? "RED WINS" : "BLACK WINS") : won ? "YOU WIN" : "YOU LOSE");
    UI.setText("hint", "ENTER: REMATCH   ESC: LEAVE");
  } else {
    UI.setText("turn", myTurn() ? (board.chain >= 0 ? "JUMP AGAIN" : "YOUR MOVE") : "THEIR MOVE");
    UI.setText("hint", "ARROWS OR MOUSE PICK · ENTER OR CLICK MOVES · ESC LEAVES");
  }
}

// ---- Playing ----------------------------------------------------------------------

function act(index: i32): void {
  if (index < 0 || !myTurn()) return;
  if (sideOf(board.cells[index]) == board.turn && board.chain < 0) {
    selected = board.movesFrom(index).length > 0 ? index : -1;
    return;
  }
  const from = board.chain >= 0 ? board.chain : selected;
  if (from < 0) return;
  const moves = board.movesFrom(from);
  for (let i = 0; i < moves.length; i++) {
    if (moves[i].to == index) {
      me.send("move", from.toString() + "," + index.toString());  // to the host's copy
      selected = -1;
      return;
    }
  }
}

function moveCursor(): void {
  const dc = (Input.justPressed("right") ? 1 : 0) - (Input.justPressed("left") ? 1 : 0);
  const dr = (Input.justPressed("up") ? 1 : 0) - (Input.justPressed("down") ? 1 : 0);
  if (dc == 0 && dr == 0) return;
  // In this player's view, then back to the board's rows and columns.
  let r = flip ? 7 - rowOf(cursor) : rowOf(cursor);
  let c = flip ? 7 - colOf(cursor) : colOf(cursor);
  r = min(7, max(0, r + dr));
  c = min(7, max(0, c + dc));
  cursor = flip ? cell(7 - r, 7 - c) : cell(r, c);
}

export function onUpdate(dt: f32): void {
  const game = me.data.getString("game");
  if (game.length > 0 && game != seen) {
    seen = game;
    board = Board.decode(game);
    if (selected >= 0 && board.movesFrom(selected).length == 0) selected = -1;
  }

  // The opponent left (or this machine lost them): back to the title.
  if (Net.online && (Net.status == NetStatus.Disconnected || Net.players().length < 2)) {
    if (!opponentGone) {
      opponentGone = true;
      leaving.start(2.5);
      UI.setText("turn", "OPPONENT LEFT");
      UI.setText("hint", "");
    }
    if (leaving.tick(dt)) {
      Net.leave();
      GameState.setString(NOTE, "THE OPPONENT LEFT");
      Scene.load("title");
    }
    return;
  }
  if (Input.justPressed("back")) {
    Net.leave();
    Scene.load("title");
    return;
  }

  if (board.winner != 0) {
    if (Input.justPressed("confirm") || Input.justPressed("click")) me.send("rematch");
  } else {
    if (myTurn()) moveCursor();
    if (Input.justPressed("confirm")) act(cursor);
    if (Input.justPressed("click")) {
      Input.pointer(pointer);
      Camera.toWorld(pointer.x, pointer.y, world);
      const index = indexAt(world.x, world.y);
      if (index >= 0) cursor = index;
      act(index);
    }
  }
  draw();
}
