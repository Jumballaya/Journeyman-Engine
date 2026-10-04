// The seven tetrominoes: shapes in all four rotations, colors, and the SRS
// wall kicks tried when a rotation is blocked. Coordinates are x right, y up.

export enum Kind { I, O, T, S, Z, J, L }
export const KIND_COUNT = 7;
export const GARBAGE: Kind = 7;  // preset board cells; has a color but no shape

// Spawn orientation, top row first, inside the piece's rotation box.
const SHAPES: string[][] = [
  ["....", "IIII", "....", "...."],
  ["OO", "OO"],
  [".T.", "TTT", "..."],
  [".SS", "SS.", "..."],
  ["ZZ.", ".ZZ", "..."],
  ["J..", "JJJ", "..."],
  ["..L", "LLL", "..."],
];

const COLORS: f32[] = [  // r, g, b per kind
  0.2, 0.85, 0.95,   // I cyan
  0.98, 0.85, 0.2,   // O yellow
  0.7, 0.35, 0.9,    // T purple
  0.35, 0.85, 0.35,  // S green
  0.95, 0.3, 0.3,    // Z red
  0.25, 0.45, 0.95,  // J blue
  0.98, 0.6, 0.2,    // L orange
  0.5, 0.52, 0.6,    // garbage gray
];

// Cell offsets per [kind][rotation]: x0, y0, x1, y1, ... relative to the box's
// top-left corner (so y offsets are 0 or negative).
const CELLS: i32[][][] = buildCells();

function buildCells(): i32[][][] {
  const all = new Array<i32[][]>();
  for (let k = 0; k < KIND_COUNT; k++) {
    const rows = SHAPES[k];
    const n = rows.length;
    const rotations = new Array<i32[]>();
    for (let r = 0; r < 4; r++) {
      const cells = new Array<i32>();
      for (let row = 0; row < n; row++) {
        for (let col = 0; col < n; col++) {
          // Rotating the box clockwise r times maps (row, col) back to the spawn cell below.
          let sr = row, sc = col;
          for (let i = 0; i < r; i++) {
            const t = sr;
            sr = n - 1 - sc;
            sc = t;
          }
          if (rows[sr].charAt(sc) != ".") {
            cells.push(col);
            cells.push(-row);
          }
        }
      }
      rotations.push(cells);
    }
    all.push(rotations);
  }
  return all;
}

// Offsets of the piece's four cells: x0, y0, x1, y1, ...
export function cellsOf(kind: Kind, rotation: i32): i32[] {
  return CELLS[kind][rotation & 3];
}

export function red(kind: Kind): f32 { return COLORS[kind * 3]; }
export function green(kind: Kind): f32 { return COLORS[kind * 3 + 1]; }
export function blue(kind: Kind): f32 { return COLORS[kind * 3 + 2]; }

// "IOTSZJL" letters to kinds; unknown letters are skipped.
export function kindsFrom(letters: string): Kind[] {
  const out = new Array<Kind>();
  for (let i = 0; i < letters.length; i++) {
    const k = "IOTSZJL".indexOf(letters.charAt(i));
    if (k >= 0) out.push(<Kind>k);
  }
  return out;
}

// SRS kick tests for clockwise turns out of rotation 0, 1, 2, 3: five (dx, dy)
// pairs each. A counter-clockwise turn uses the negated reverse clockwise turn.
const KICKS_JLSTZ: i32[] = [
  0, 0, -1, 0, -1, 1, 0, -2, -1, -2,
  0, 0, 1, 0, 1, -1, 0, 2, 1, 2,
  0, 0, 1, 0, 1, 1, 0, -2, 1, -2,
  0, 0, -1, 0, -1, -1, 0, 2, -1, 2,
];
const KICKS_I: i32[] = [
  0, 0, -2, 0, 1, 0, -2, -1, 1, 2,
  0, 0, -1, 0, 2, 0, -1, 2, 2, -1,
  0, 0, 2, 0, -1, 0, 2, 1, -1, -2,
  0, 0, 1, 0, -2, 0, 1, -2, -2, 1,
];
export const KICK_TESTS = 5;

// The `test`th kick for turning `kind` from `rotation` by `turn` (+1 cw, -1 ccw).
export function kickX(kind: Kind, rotation: i32, turn: i32, test: i32): i32 {
  return kick(kind, rotation, turn, test, 0);
}
export function kickY(kind: Kind, rotation: i32, turn: i32, test: i32): i32 {
  return kick(kind, rotation, turn, test, 1);
}

function kick(kind: Kind, rotation: i32, turn: i32, test: i32, axis: i32): i32 {
  if (kind == Kind.O) return 0;
  const table = kind == Kind.I ? KICKS_I : KICKS_JLSTZ;
  if (turn > 0) return table[(rotation & 3) * 10 + test * 2 + axis];
  return -table[((rotation + 3) & 3) * 10 + test * 2 + axis];  // reverse of the cw turn into `rotation`
}
