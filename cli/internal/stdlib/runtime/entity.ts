import {
  __jmSelf, __jmEntityIsAlive, __jmEntityHasTag, __jmEntitySetTag,
  __jmWorldFindFirst, __jmWorldFindAll, __jmWorldSpawn, __jmWorldDestroy,
} from "./env";
import { utf8 } from "./util";

// A handle to an entity: (index, generation). Handles stay valid to hold
// across frames — `isAlive` turns false once the entity is destroyed, and a
// recycled index gets a new generation, so a stale handle never aliases.
export class Entity {
  static readonly NONE: Entity = new Entity(0xFFFFFFFF, 0xFFFFFFFF);

  constructor(public readonly index: u32, public readonly generation: u32) {}

  static unpack(packed: i64): Entity {
    if (packed == -1) return Entity.NONE;
    return new Entity(<u32>(packed & 0xFFFFFFFF), <u32>(<u64>packed >> 32));
  }

  // The entity this script is attached to.
  static self(): Entity {
    return Entity.unpack(__jmSelf());
  }

  get isValid(): bool {
    return this.index != 0xFFFFFFFF;
  }

  // False once destroyed or scheduled for destruction this frame.
  get isAlive(): bool {
    return this.isValid && __jmEntityIsAlive(<i32>this.index, <i32>this.generation) != 0;
  }

  equals(other: Entity): bool {
    return this.index == other.index && this.generation == other.generation;
  }

  hasTag(tag: string): bool {
    const t = utf8(tag);
    return __jmEntityHasTag(<i32>this.index, <i32>this.generation, <i32>t.dataStart, t.length - 1) != 0;
  }

  addTag(tag: string): void {
    const t = utf8(tag);
    __jmEntitySetTag(<i32>this.index, <i32>this.generation, <i32>t.dataStart, t.length - 1, 1);
  }

  removeTag(tag: string): void {
    const t = utf8(tag);
    __jmEntitySetTag(<i32>this.index, <i32>this.generation, <i32>t.dataStart, t.length - 1, 0);
  }

  // Destruction is deferred to the end of the frame; isAlive is false
  // immediately and the entity stops receiving collisions.
  destroy(): void {
    if (this.isValid) __jmWorldDestroy(<i32>this.index, <i32>this.generation);
  }
}

export class World {
  // Instantiates a prefab at (x, y) at the end of this frame and returns its
  // handle right away. `overridesJson` uses the scene-file override shape:
  //   '{"VelocityComponent": {"velocity": [0, 300]}}'
  // Spawned entities belong to the current scene and die with it.
  static spawn(prefabPath: string, x: f32, y: f32, overridesJson: string = ""): Entity {
    const p = utf8(prefabPath);
    const o = utf8(overridesJson);
    return Entity.unpack(__jmWorldSpawn(<i32>p.dataStart, p.length - 1, x, y, <i32>o.dataStart, o.length - 1));
  }

  // Any live entity with the tag, or Entity.NONE.
  static find(tag: string): Entity {
    const t = utf8(tag);
    return Entity.unpack(__jmWorldFindFirst(<i32>t.dataStart, t.length - 1));
  }

  static findAll(tag: string): Entity[] {
    const t = utf8(tag);
    let cap = 64;
    let buf = new Uint32Array(cap * 2);
    let count = __jmWorldFindAll(<i32>t.dataStart, t.length - 1, <i32>buf.dataStart, cap);
    if (count > cap) {
      cap = count;
      buf = new Uint32Array(cap * 2);
      count = min(__jmWorldFindAll(<i32>t.dataStart, t.length - 1, <i32>buf.dataStart, cap), cap);
    }
    const out = new Array<Entity>(count);
    for (let i = 0; i < count; i++) out[i] = new Entity(buf[i * 2], buf[i * 2 + 1]);
    return out;
  }

  static count(tag: string): i32 {
    const t = utf8(tag);
    return __jmWorldFindAll(<i32>t.dataStart, t.length - 1, 0, 0);
  }
}
