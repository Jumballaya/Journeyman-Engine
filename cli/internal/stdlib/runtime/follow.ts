import { Entity, Transform } from "./entity";

// An explicitly owned companion (marker, escort, etc). Call follow each frame
// and destroy when the owner dies. Scene unload also destroys the companion.
export class TransformFollower {
  private readonly transform: Transform;
  constructor(readonly entity: Entity, readonly dx: f32 = 0, readonly dy: f32 = 0, readonly copyRotation: bool = true) {
    this.transform = entity.transform;
  }
  follow(target: Transform): void {
    // Prefab components arrive at the end of the spawn frame.
    if (!this.entity.has("TransformComponent") || !target.entity.has("TransformComponent")) return;
    this.transform.setPosition(target.x + this.dx, target.y + this.dy);
    if (this.copyRotation) this.transform.rotation = target.rotation;
  }
  destroy(): void { this.entity.destroy(); }
}

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
