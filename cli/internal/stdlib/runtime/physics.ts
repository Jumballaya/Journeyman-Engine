import { __jmPhysicsOverlap, __jmPhysicsRaycast } from "./env";
import { Entity } from "./entity";

// What a ray met: the collider, where, its surface's normal there (facing the
// ray) and how far along the ray.
export class RayHit {
  constructor(readonly entity: Entity, readonly x: f32, readonly y: f32, readonly normalX: f32, readonly normalY: f32,
              readonly distance: f32) {}
}

const hitBytes = new ArrayBuffer(28);

// Asking where colliders are (boxes and circles), on the layers in mask (all
// by default). Lists come in world order.
export class Physics {
  // The first collider a ray from (x, y) toward (dx, dy) meets within
  // distance, skipping ignore (say, the caster): null when nothing is there.
  static raycast(x: f32, y: f32, dx: f32, dy: f32, distance: f32, mask: u32 = 0xFFFFFFFF,
                 ignore: Entity = Entity.NONE): RayHit | null {
    const out = changetype<usize>(hitBytes);
    if (!__jmPhysicsRaycast(x, y, dx, dy, distance, mask, ignore.index, ignore.generation, out, 28)) return null;
    return new RayHit(new Entity(load<u32>(out), load<u32>(out, 4)), load<f32>(out, 8), load<f32>(out, 12),
                      load<f32>(out, 16), load<f32>(out, 20), load<f32>(out, 24));
  }

  // The colliders overlapping a circle.
  static overlapCircle(x: f32, y: f32, radius: f32, mask: u32 = 0xFFFFFFFF): Entity[] {
    return overlap(x, y, 0, 0, radius, mask);
  }

  // The colliders overlapping a box (half width and height, from its center).
  static overlapBox(x: f32, y: f32, halfWidth: f32, halfHeight: f32, mask: u32 = 0xFFFFFFFF): Entity[] {
    return overlap(x, y, halfWidth, halfHeight, 0, mask);
  }

  // The colliders under a point.
  static at(x: f32, y: f32, mask: u32 = 0xFFFFFFFF): Entity[] {
    return overlap(x, y, 0, 0, 0, mask);
  }
}

function overlap(x: f32, y: f32, halfW: f32, halfH: f32, radius: f32, mask: u32): Entity[] {
  let ids = new Uint32Array(64);
  let count = __jmPhysicsOverlap(x, y, halfW, halfH, radius, mask, ids.dataStart, ids.byteLength);
  if (count * 2 > ids.length) {
    ids = new Uint32Array(count * 2);
    count = min(__jmPhysicsOverlap(x, y, halfW, halfH, radius, mask, ids.dataStart, ids.byteLength), count);
  }
  const out = new Array<Entity>(count);
  for (let i = 0; i < count; i++) out[i] = new Entity(ids[i * 2], ids[i * 2 + 1]);
  return out;
}
