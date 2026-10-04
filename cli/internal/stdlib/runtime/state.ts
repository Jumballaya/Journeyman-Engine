import {
  __jmStateSetNumber, __jmStateGetNumber, __jmStateSetString, __jmStateGetString,
  __jmStateHas, __jmStateRemove, __jmStateClear,
} from "./env";
import { utf8, scratchPtr, scratchCap, needsRetry, scratchString } from "./util";

class Store {
  constructor(private readonly id: i32) {}

  getNumber(key: string, fallback: f64 = 0): f64 {
    const k = utf8(key);
    return __jmStateGetNumber(this.id, <i32>k.dataStart, k.length - 1, fallback);
  }

  setNumber(key: string, value: f64): void {
    const k = utf8(key);
    __jmStateSetNumber(this.id, <i32>k.dataStart, k.length - 1, value);
  }

  add(key: string, delta: f64): f64 {
    const v = this.getNumber(key) + delta;
    this.setNumber(key, v);
    return v;
  }

  getString(key: string, fallback: string = ""): string {
    const k = utf8(key);
    let n = __jmStateGetString(this.id, <i32>k.dataStart, k.length - 1, scratchPtr(), scratchCap());
    if (needsRetry(n)) n = __jmStateGetString(this.id, <i32>k.dataStart, k.length - 1, scratchPtr(), scratchCap());
    return n < 0 ? fallback : scratchString(n);
  }

  setString(key: string, value: string): void {
    const k = utf8(key);
    const v = utf8(value);
    __jmStateSetString(this.id, <i32>k.dataStart, k.length - 1, <i32>v.dataStart, v.length - 1);
  }

  has(key: string): bool {
    const k = utf8(key);
    return __jmStateHas(this.id, <i32>k.dataStart, k.length - 1) != 0;
  }

  remove(key: string): void {
    const k = utf8(key);
    __jmStateRemove(this.id, <i32>k.dataStart, k.length - 1);
  }

  clear(): void {
    __jmStateClear(this.id);
  }
}

// Session state shared by every script and kept across scene changes
// (score, lives, current stage). Lost when the game exits.
export const GameState: Store = new Store(0);

// Persistent state written to the player's save file (high score, settings).
export const Save: Store = new Store(1);
