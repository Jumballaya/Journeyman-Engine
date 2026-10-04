import {
  __jmStateGetNumber, __jmStateSetNumber, __jmStateGetString, __jmStateSetString,
  __jmStateHas, __jmStateRemove, __jmStateClear,
} from "./env";
import { utf8, buf, cap, grow, text } from "./util";

// A key/value store shared by every script.
export class Store {
  constructor(private readonly store: i32) {}

  getNumber(key: string, fallback: f64 = 0): f64 {
    const k = utf8(key);
    return __jmStateGetNumber(this.store, k.dataStart, k.length, fallback);
  }

  setNumber(key: string, value: f64): void {
    const k = utf8(key);
    __jmStateSetNumber(this.store, k.dataStart, k.length, value);
  }

  // Adds `delta` and returns the new value.
  add(key: string, delta: f64): f64 {
    const v = this.getNumber(key) + delta;
    this.setNumber(key, v);
    return v;
  }

  getBool(key: string, fallback: bool = false): bool { return this.getNumber(key, fallback ? 1 : 0) != 0; }
  setBool(key: string, value: bool): void { this.setNumber(key, value ? 1 : 0); }

  // Consume a numeric request from another script (e.g. a screen flash).
  takeNumber(key: string, fallback: f64 = 0): f64 {
    const value = this.getNumber(key, fallback);
    this.remove(key);
    return value;
  }

  // Update a record only when exceeded; report whether this set a new record.
  record(key: string, candidate: f64, fallback: f64 = 0): bool {
    if (candidate <= this.getNumber(key, fallback)) return false;
    this.setNumber(key, candidate);
    return true;
  }

  getString(key: string, fallback: string = ""): string {
    const k = utf8(key);
    let n = __jmStateGetString(this.store, k.dataStart, k.length, buf(), cap());
    if (grow(n)) n = __jmStateGetString(this.store, k.dataStart, k.length, buf(), cap());
    return text(n, fallback);
  }

  setString(key: string, value: string): void {
    const k = utf8(key);
    const v = utf8(value);
    __jmStateSetString(this.store, k.dataStart, k.length, v.dataStart, v.length);
  }

  has(key: string): bool {
    const k = utf8(key);
    return __jmStateHas(this.store, k.dataStart, k.length);
  }

  remove(key: string): void {
    const k = utf8(key);
    __jmStateRemove(this.store, k.dataStart, k.length);
  }

  clear(): void { __jmStateClear(this.store); }
}

// Survives scene changes, lost on exit: score, lives, current stage.
export const GameState: Store = new Store(0);

// Written to the player's save file: high scores, settings.
export const Save: Store = new Store(1);

// A named checkpoint of numeric keys kept in the host store, so another script
// instance/scene can restore it. Prefix must be reserved for this snapshot.
export class NumberSnapshot {
  private readonly keys: string[];
  constructor(private store: Store, private prefix: string, keys: string[]) { this.keys = keys.slice(); }
  capture(): void {
    for (let i = 0; i < this.keys.length; i++) {
      const key = this.keys[i];
      const saved = this.prefix + "." + key;
      if (this.store.has(key)) this.store.setNumber(saved, this.store.getNumber(key));
      else this.store.remove(saved);
    }
    this.store.setBool(this.prefix + ".captured", true);
  }
  restore(): bool {
    if (!this.store.getBool(this.prefix + ".captured")) return false;
    for (let i = 0; i < this.keys.length; i++) {
      const key = this.keys[i];
      const saved = this.prefix + "." + key;
      if (this.store.has(saved)) this.store.setNumber(key, this.store.getNumber(saved));
      else this.store.remove(key);
    }
    return true;
  }
}
