// String marshalling shared by the runtime modules. Internal.

// UTF-8 bytes of `s`; pass `.dataStart` and `.length` to the host.
export function utf8(s: string): Uint8Array {
  return Uint8Array.wrap(String.UTF8.encode(s));
}

// Host string results: the host copies up to cap() bytes to buf() and
// returns the full length (-1 = none). If grow(n) is true, call again.
let scratch = new Uint8Array(256);

export function buf(): usize { return scratch.dataStart; }
export function cap(): i32 { return scratch.length; }

export function grow(n: i32): bool {
  if (n <= scratch.length) return false;
  scratch = new Uint8Array(n);
  return true;
}

export function text(n: i32, fallback: string): string {
  return n < 0 ? fallback : String.UTF8.decodeUnsafe(scratch.dataStart, <usize>min(n, scratch.length));
}
