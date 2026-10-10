import { __jmPhysicsOverlap, __jmPhysicsRaycast } from "./env";
import { Entity } from "./entity";

// What a ray met: the collider, where, its surface's normal there (facing the
// ray) and how far along the ray.
export class RayHit {
  constructor(readonly entity: Entity, readonly x: f32, readonly y: f32, readonly normalX: f32, readonly normalY: f32,
              readonly distance: f32) {}
}

const hit = new StaticArray<u32>(7);  // the host's RaycastOut: index, generation, then x, y, nx, ny, distance as f32

// Asking where colliders (boxes and circles) and drawn ground are without moving
// anything. Each query can skip one entity (ignore: the caster, say) and look
// only at the layers in mask (default: all). Lists: boxes, circles, then ground.
export class Physics {
  // The first collider or ground a ray from (x, y) toward (dx, dy) meets within
  // distance (Infinity: no limit), else null. A ray starting inside a collider
  // hits it at 0 (a probe from an entity ignores it); ground it starts on, it passes.
  static raycast(x: f32, y: f32, dx: f32, dy: f32, distance: f32, ignore: Entity = Entity.NONE,
                 mask: u32 = 0xFFFFFFFF): RayHit | null {
    if (!__jmPhysicsRaycast(x, y, dx, dy, distance, mask, ignore.index, ignore.generation, changetype<usize>(hit), 28)) return null;
    return new RayHit(new Entity(hit[0], hit[1]), reinterpret<f32>(hit[2]), reinterpret<f32>(hit[3]),
                      reinterpret<f32>(hit[4]), reinterpret<f32>(hit[5]), reinterpret<f32>(hit[6]));
  }

  // The colliders and ground overlapping a circle.
  static overlapCircle(x: f32, y: f32, radius: f32, ignore: Entity = Entity.NONE, mask: u32 = 0xFFFFFFFF): Entity[] {
    return overlap(CIRCLE, x, y, 0, 0, radius, ignore, mask);
  }

  // The colliders and ground overlapping a box (half width and height, from its center).
  static overlapBox(x: f32, y: f32, halfWidth: f32, halfHeight: f32, ignore: Entity = Entity.NONE,
                    mask: u32 = 0xFFFFFFFF): Entity[] {
    return overlap(BOX, x, y, halfWidth, halfHeight, 0, ignore, mask);
  }

  // The colliders under a point (ground has no area: never).
  static at(x: f32, y: f32, ignore: Entity = Entity.NONE, mask: u32 = 0xFFFFFFFF): Entity[] {
    return overlap(BOX, x, y, 0, 0, 0, ignore, mask);
  }
}

const BOX = 0, CIRCLE = 1;

function overlap(kind: i32, x: f32, y: f32, halfW: f32, halfH: f32, radius: f32, ignore: Entity, mask: u32): Entity[] {
  let ids = new Uint32Array(64);
  let count = __jmPhysicsOverlap(kind, x, y, halfW, halfH, radius, mask, ignore.index, ignore.generation, ids.dataStart, ids.byteLength);
  if (count * 2 > ids.length) {
    ids = new Uint32Array(count * 2);
    count = min(__jmPhysicsOverlap(kind, x, y, halfW, halfH, radius, mask, ignore.index, ignore.generation, ids.dataStart,
                                   ids.byteLength), count);
  }
  const out = new Array<Entity>(count);
  for (let i = 0; i < count; i++) out[i] = new Entity(ids[i * 2], ids[i * 2 + 1]);
  return out;
}
