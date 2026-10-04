import { GameState, Store, NumberSnapshot } from "./state";

// A declared key in a shared store. Handles read the host on every access, so
// scripts in different entities/scenes always see the same value.
export abstract class StateEntry {
  constructor(readonly store: Store, readonly key: string) {}
  get present(): bool { return this.store.has(this.key); }
  abstract reset(): void;
}

export class StateNumber<T> extends StateEntry {
  constructor(store: Store, key: string, readonly initial: T,
    private minimum: f64 = -Infinity, private maximum: f64 = Infinity,
    private step: f64 = 0) { super(store, key); }
  get value(): T { return <T>this.store.getNumber(this.key, <f64>this.initial); }
  set value(value: T) {
    let n = <f64>value;
    if (!isFinite(n)) return;
    if (this.step > 0) n = Math.round(n / this.step) * this.step;
    this.store.setNumber(this.key, Math.max(this.minimum, Math.min(this.maximum, n)));
  }
  add(delta: T): T { this.value = this.value + delta; return this.value; }
  // A high score, or a strongest-wins request such as a screen flash.
  record(candidate: T): bool {
    if (candidate <= this.value) return false;
    this.value = candidate;
    return true;
  }
  take(): T { const value = this.value; this.reset(); return value; }
  reset(): void { this.store.remove(this.key); }
}

export class StateFlag extends StateEntry {
  constructor(store: Store, key: string, readonly initial: bool = false) { super(store, key); }
  get value(): bool { return this.store.getBool(this.key, this.initial); }
  set value(on: bool) { this.store.setBool(this.key, on); }
  toggle(): bool { this.value = !this.value; return this.value; }
  reset(): void { this.store.remove(this.key); }
}

// Owns declarations and reset groups, not a cached copy of game state.
// Empty prefixes preserve existing keys; use a prefix to isolate a subsystem.
export class Session {
  private entries: StateEntry[] = [];
  private groups: Session[] = [];
  constructor(private prefix: string = "", readonly store: Store = GameState) {}
  protected track(entry: StateEntry): void { this.entries.push(entry); }
  number<T>(key: string, initial: T, minimum: f64 = -Infinity, maximum: f64 = Infinity, step: f64 = 0): StateNumber<T> {
    const entry = new StateNumber<T>(this.store, this.prefix + key, initial, minimum, maximum, step);
    this.entries.push(entry);
    return entry;
  }
  flag(key: string, initial: bool = false): StateFlag {
    const entry = new StateFlag(this.store, this.prefix + key, initial);
    this.entries.push(entry);
    return entry;
  }
  group(prefix: string = ""): Session {
    const group = new Session(this.prefix + prefix, this.store);
    this.groups.push(group);
    return group;
  }
  // Restores defaults for this group and its children; unrelated keys survive.
  reset(): void {
    for (let i = 0; i < this.entries.length; i++) this.entries[i].reset();
    for (let i = 0; i < this.groups.length; i++) this.groups[i].reset();
  }
  checkpoint(name: string, entries: StateEntry[]): Checkpoint {
    const keys: string[] = [];
    for (let i = 0; i < entries.length; i++) {
      assert(entries[i].store === this.store, "Checkpoint entries must use the same store");
      keys.push(entries[i].key);
    }
    return new Checkpoint(this.store, this.prefix + name, keys);
  }
}

export class Checkpoint extends NumberSnapshot {
  constructor(private checkpointStore: Store, private name: string, keys: string[]) { super(checkpointStore, name, keys); }
  // Capture the first attempt of a level, quest, etc. Retries keep its snapshot.
  captureOnce(token: f64): void {
    const key = this.name + ".token";
    if (this.checkpointStore.has(key) && this.checkpointStore.getNumber(key) == token) return;
    this.capture();
    this.checkpointStore.setNumber(key, token);
  }
  forget(): void {
    this.checkpointStore.remove(this.name + ".token");
    this.checkpointStore.remove(this.name + ".captured");
  }
}
