import { Entity } from "./entity";

// Damage once per live source (e.g. overlapping bomb areas). Uses the full
// generation-safe handle; recycled entity indices count as new sources.
export class HitHistory {
  private sources: Entity[] = [];
  accept(source: Entity): bool {
    if (!source.isAlive) return false;
    for (let i = this.sources.length - 1; i >= 0; i--) {
      if (!this.sources[i].isAlive) this.sources.splice(i, 1);
      else if (this.sources[i].equals(source)) return false;
    }
    this.sources.push(source);
    return true;
  }
  clear(): void { this.sources.length = 0; }
}
