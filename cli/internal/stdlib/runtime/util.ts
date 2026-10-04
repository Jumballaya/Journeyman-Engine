// Internal helpers shared by the runtime modules. Not exported from index.

// NUL-terminated UTF-8 bytes; pass `.dataStart` and `.length - 1` to hosts.
export function utf8(s: string): Uint8Array {
  return Uint8Array.wrap(String.UTF8.encode(s, true));
}

// Scratch buffer for host functions that follow the "copy up to capacity,
// return the full length (-1 = missing)" convention. Usage:
//   let n = host(..., scratchPtr(), scratchCap());
//   if (needsRetry(n)) n = host(..., scratchPtr(), scratchCap());
//   return n < 0 ? fallback : scratchString(n);
let scratch = new Uint8Array(256);

export function scratchPtr(): i32 { return <i32>scratch.dataStart; }
export function scratchCap(): i32 { return scratch.length; }

// Grows the scratch buffer when `len` didn't fit; true means call again.
export function needsRetry(len: i32): bool {
  if (len <= scratch.length) return false;
  scratch = new Uint8Array(len);
  return true;
}

export function scratchString(len: i32): string {
  return String.UTF8.decodeUnsafe(scratch.dataStart, <usize>len);
}
