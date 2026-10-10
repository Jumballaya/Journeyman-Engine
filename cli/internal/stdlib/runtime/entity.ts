import {
  __jmSelf, __jmEntityIsAlive, __jmEntityHasTag, __jmEntitySetTag, __jmEntityHasComponent,
  __jmWorldDestroy, __jmFieldId, __jmFieldGet, __jmFieldSet, __jmSpritePlay, __jmSpriteFinished,
  __jmSpriteAnimation, __jmSpriteSetTexture, __jmEntityStore, __jmEntitySend, __jmTextSet,
  __jmEntityParent, __jmEntityChildren, __jmEntityAttach, __jmPhysicsMove, __jmPhysicsWalk,
  __jmNetIsMine, __jmNetIsShared, __jmNetOwner, __jmNetController, __jmNetSendEntity,
} from "./env";
import { EntityParams } from "./params";
import { Store } from "./state";
import { utf8, buf, cap, grow, text } from "./util";

// One script-visible field of a component, e.g. new Field("HealthComponent", "hp")
// for your own C++ components (see ComponentSpec::scriptFields).
export class Field {
  readonly id: i32;

  constructor(component: string, field: string) {
    const c = utf8(component);
    const f = utf8(field);
    this.id = __jmFieldId(c.dataStart, c.length, f.dataStart, f.length);
  }

  // 0 if the entity is dead or lacks the component.
  get(entity: Entity): f32 { return reinterpret<f32>(this.bits(entity)); }
  // Ignored if the entity is dead or lacks the component.
  set(entity: Entity, value: f32): void { this.setBits(entity, reinterpret<u32>(value)); }

  bits(entity: Entity): u32 { return __jmFieldGet(entity.index, entity.generation, this.id); }
  setBits(entity: Entity, value: u32): void { __jmFieldSet(entity.index, entity.generation, this.id, value); }
}

// A handle to an entity. Safe to keep across frames: once the entity is
// destroyed `isAlive` turns false and component access does nothing.
export class Entity {
  static readonly NONE: Entity = new Entity(0xFFFFFFFF, 0xFFFFFFFF);

  constructor(readonly index: u32, readonly generation: u32) {}

  static unpack(packed: i64): Entity {
    return packed == -1 ? Entity.NONE : new Entity(<u32>packed, <u32>(<u64>packed >> 32));
  }

  get isNone(): bool { return this.index == 0xFFFFFFFF; }
  // False once destroyed or scheduled for destruction.
  get isAlive(): bool { return !this.isNone && __jmEntityIsAlive(this.index, this.generation); }
  equals(other: Entity): bool { return this.index == other.index && this.generation == other.generation; }

  hasTag(tag: string): bool {
    const t = utf8(tag);
    return __jmEntityHasTag(this.index, this.generation, t.dataStart, t.length);
  }
  addTag(tag: string): void { this.setTag(tag, true); }
  removeTag(tag: string): void { this.setTag(tag, false); }

  // e.g. has("VelocityComponent")
  has(component: string): bool {
    const c = utf8(component);
    return __jmEntityHasComponent(this.index, this.generation, c.dataStart, c.length);
  }

  // Removed at the end of the frame; stops colliding immediately.
  destroy(): void {
    if (!this.isNone) __jmWorldDestroy(this.index, this.generation);
  }

  // Its script's params (read-only), e.g. door.params.text("key").
  get params(): EntityParams { return new EntityParams(this.index, this.generation); }
  // Values any script can read and write on this entity, dropped with it.
  get data(): Store { return new Store(this.isAlive ? __jmEntityStore(this.index, this.generation) : -1); }

  // Delivers a message to the entity's script (its onMessage(message) export)
  // before its next update; dropped if it has no script. A shared entity
  // (multiplayer) gets it on the machine that simulates it.
  send(name: string, text: string = "", number: f64 = 0): void {
    const n = utf8(name);
    const t = utf8(text);
    __jmEntitySend(this.index, this.generation, n.dataStart, n.length, t.dataStart, t.length, number);
  }

  // ---- Multiplayer (Network component) ----
  // Whether this machine simulates it: always for unshared entities and offline.
  get isMine(): bool { return this.isNone || __jmNetIsMine(this.index, this.generation); }
  // Whether it has a Network component.
  get isShared(): bool { return !this.isNone && __jmNetIsShared(this.index, this.generation); }
  // The player who simulates it, or Net.HOST (-1) for the host.
  get owner(): i32 { return this.isNone ? -1 : __jmNetOwner(this.index, this.generation); }
  // The player whose input its script reads, or -1 for none.
  get controller(): i32 { return this.isNone ? -1 : __jmNetController(this.index, this.generation); }
  // Like send, but to every copy, on every machine (this one included).
  broadcast(name: string, text: string = "", number: f64 = 0): void {
    const n = utf8(name);
    const t = utf8(text);
    __jmNetSendEntity(this.index, this.generation, n.dataStart, n.length, t.dataStart, t.length, number, true);
  }

  // ---- Parent and children (nesting in scenes and prefabs) ----
  // A child moves and turns with its parent and is destroyed with it; its
  // transform is where that puts it in the world, and `local` is its place
  // relative to the parent. Entity.NONE when it has none.
  get parent(): Entity { return this.isNone ? Entity.NONE : Entity.unpack(__jmEntityParent(this.index, this.generation)); }
  get children(): Entity[] {
    let ids = new Uint32Array(32);
    let count = __jmEntityChildren(this.index, this.generation, ids.dataStart, ids.byteLength);
    if (count * 2 > ids.length) {
      ids = new Uint32Array(count * 2);
      count = min(__jmEntityChildren(this.index, this.generation, ids.dataStart, ids.byteLength), count);
    }
    const out = new Array<Entity>(count);
    for (let i = 0; i < count; i++) out[i] = new Entity(ids[i * 2], ids[i * 2 + 1]);
    return out;
  }
  // The child with this name (as named in the scene or prefab), or Entity.NONE.
  child(name: string): Entity {
    const all = this.children;
    for (let i = 0; i < all.length; i++) if (all[i].hasTag(name)) return all[i];
    return Entity.NONE;
  }
  // Makes it a child of `parent`, staying where it is; applied at the end of the
  // frame (so a just-spawned entity can be attached). detach() lets it go.
  attach(parent: Entity): void {
    if (!this.isNone) __jmEntityAttach(this.index, this.generation, parent.index, parent.generation);
  }
  detach(): void { this.attach(Entity.NONE); }
  get local(): LocalTransform { return new LocalTransform(this); }

  // Moves it by (dx, dy) without entering colliders solid to it (their
  // blocksMask meets its collider's layerMask) or drawn ground on its layers:
  // along x, then y, stopping flush against what's in the way, so it slides
  // along walls and lands on floors. With `slide` > 0, a blocked move nudges up
  // to `slide` units sideways toward an opening (among boxes, not near drawn
  // ground). A solid mover (or ground) is a moving platform: what stands on it
  // (solid boxes only with a velocity, so not walls) goes along: across, each
  // meeting walls on its own; up together, as far as all can (what stops one is
  // its byY); down after it. Moving them, it doesn't slide. A solid one pushes
  // what it runs into that has a velocity; with nowhere to go, that stays in it:
  // crushed. A body goes across with one moving platform a frame: the first to.
  // Needs a collider, no parent.
  move(dx: f32, dy: f32, slide: f32 = 0): Blocked {
    __jmPhysicsMove(this.index, this.generation, dx, dy, slide, changetype<usize>(moved), 32);
    return blocked();
  }

  // Moves it like move(), walking: up slopes to 50° and ledges to 1 unit, down
  // slopes and steps without leaving them (unless rising), onto one-way
  // platforms from above (dropThrough: falls through them). For platformers.
  walk(dx: f32, dy: f32, dropThrough: bool = false): Blocked {
    __jmPhysicsWalk(this.index, this.generation, dx, dy, dropThrough ? 1 : 0, changetype<usize>(moved), 32);
    return blocked();
  }

  get transform(): Transform { return new Transform(this); }
  get velocity(): Velocity { return new Velocity(this); }
  get sprite(): Sprite { return new Sprite(this); }
  get collider(): Collider { return new Collider(this); }
  get circle(): CircleCollider { return new CircleCollider(this); }
  get lifetime(): Lifetime { return new Lifetime(this); }
  get particles(): Particles { return new Particles(this); }
  get text(): Text { return new Text(this); }

  private setTag(tag: string, present: bool): void {
    const t = utf8(tag);
    __jmEntitySetTag(this.index, this.generation, t.dataStart, t.length, present);
  }
}

const moved = new StaticArray<i32>(8);  // the host's MoveOut: hits, then by x, by y, then the normal as f32
function blocked(): Blocked {
  return new Blocked(moved[0], moved[1], new Entity(<u32>moved[2], <u32>moved[3]), new Entity(<u32>moved[4], <u32>moved[5]),
                     reinterpret<f32>(moved[6]), reinterpret<f32>(moved[7]));
}

// What stopped a move(): -1/+1 for the side blocked on each axis, the entity
// in the way along each (Entity.NONE if nothing), and the normal of the surface
// met along y, facing it (standing: the ground's, leaning on slopes; 0, 0 if none).
export class Blocked {
  constructor(readonly hitX: i32, readonly hitY: i32, readonly byX: Entity, readonly byY: Entity,
              readonly normalX: f32 = 0, readonly normalY: f32 = 0) {}
  // Standing on something (blocked going down).
  get onGround(): bool { return this.hitY < 0; }
  get any(): bool { return this.hitX != 0 || this.hitY != 0; }
}

// The entity this script instance is attached to.
export function self(): Entity {
  return Entity.unpack(__jmSelf());
}

// ---- Built-in component views: live reads/writes, no copies -----------------

const TX = new Field("TransformComponent", "x");
const TY = new Field("TransformComponent", "y");
const TZ = new Field("TransformComponent", "z");
const TSX = new Field("TransformComponent", "scaleX");
const TSY = new Field("TransformComponent", "scaleY");
const TROT = new Field("TransformComponent", "rotation");

// Position is the center in world units (y up); z orders drawing (higher on
// top). Scale is the half size: sprite quads span -1..1.
export class Transform {
  constructor(readonly entity: Entity) {}
  get x(): f32 { return TX.get(this.entity); }
  set x(v: f32) { TX.set(this.entity, v); }
  get y(): f32 { return TY.get(this.entity); }
  set y(v: f32) { TY.set(this.entity, v); }
  get z(): f32 { return TZ.get(this.entity); }
  set z(v: f32) { TZ.set(this.entity, v); }
  get scaleX(): f32 { return TSX.get(this.entity); }
  set scaleX(v: f32) { TSX.set(this.entity, v); }
  get scaleY(): f32 { return TSY.get(this.entity); }
  set scaleY(v: f32) { TSY.set(this.entity, v); }
  get rotation(): f32 { return TROT.get(this.entity); }  // radians
  set rotation(v: f32) { TROT.set(this.entity, v); }

  setPosition(x: f32, y: f32): void { this.x = x; this.y = y; }
  setScale(x: f32, y: f32): void { this.scaleX = x; this.scaleY = y; }
}

const LX = new Field("LocalTransformComponent", "x");
const LY = new Field("LocalTransformComponent", "y");
const LZ = new Field("LocalTransformComponent", "z");
const LROT = new Field("LocalTransformComponent", "rotation");

// A child's place relative to its parent: offset (turned with the parent), z
// on top of the parent's, rotation added to it. Reads 0 for an entity without a parent.
export class LocalTransform {
  constructor(readonly entity: Entity) {}
  get x(): f32 { return LX.get(this.entity); }
  set x(v: f32) { LX.set(this.entity, v); }
  get y(): f32 { return LY.get(this.entity); }
  set y(v: f32) { LY.set(this.entity, v); }
  get z(): f32 { return LZ.get(this.entity); }
  set z(v: f32) { LZ.set(this.entity, v); }
  get rotation(): f32 { return LROT.get(this.entity); }  // radians
  set rotation(v: f32) { LROT.set(this.entity, v); }
  setPosition(x: f32, y: f32): void { this.x = x; this.y = y; }
}

const VX = new Field("VelocityComponent", "vx");
const VY = new Field("VelocityComponent", "vy");
const VAX = new Field("VelocityComponent", "ax");
const VAY = new Field("VelocityComponent", "ay");
const VM = new Field("VelocityComponent", "motion");
const VD = new Field("VelocityComponent", "dropThrough");
const VBX = new Field("VelocityComponent", "blockedX");
const VBY = new Field("VelocityComponent", "blockedY");
const VFI = new Field("VelocityComponent", "floorIndex");
const VFG = new Field("VelocityComponent", "floorGeneration");
const VPX = new Field("VelocityComponent", "platformVelocityX");
const VPY = new Field("VelocityComponent", "platformVelocityY");

// World units per second, applied by physics; acceleration (e.g. gravity) is
// added to it every second.
export class Velocity {
  constructor(readonly entity: Entity) {}
  get x(): f32 { return VX.get(this.entity); }
  set x(v: f32) { VX.set(this.entity, v); }
  get y(): f32 { return VY.get(this.entity); }
  set y(v: f32) { VY.set(this.entity, v); }
  set(x: f32, y: f32): void { this.x = x; this.y = y; }
  get accelerationX(): f32 { return VAX.get(this.entity); }
  get accelerationY(): f32 { return VAY.get(this.entity); }
  setAcceleration(x: f32, y: f32): void { VAX.set(this.entity, x); VAY.set(this.entity, y); }
  // "free": through everything (the default); "move" / "walk": through solids
  // and drawn ground like move() / walk(), stopping the velocity where blocked.
  get motion(): string { const m = VM.bits(this.entity); return m == 1 ? "move" : m == 2 ? "walk" : "free"; }
  set motion(m: string) { VM.setBits(this.entity, m == "move" ? 1 : m == "walk" ? 2 : 0); }
  // Walking: falls through one-way platforms while on.
  get dropThrough(): bool { return VD.bits(this.entity) != 0; }
  set dropThrough(on: bool) { VD.setBits(this.entity, on ? 1 : 0); }
  // The last step's blocked sides, -1/+1 (move/walk motion).
  get blockedX(): i32 { return <i32>VBX.get(this.entity); }
  get blockedY(): i32 { return <i32>VBY.get(this.entity); }
  get onGround(): bool { return this.blockedY < 0; }
  get onWall(): bool { return this.blockedX != 0; }
  get onCeiling(): bool { return this.blockedY > 0; }
  // What it stood on after the last step (Entity.NONE in the air), and how
  // fast that went if a velocity moved it (a moving platform): add it to a jump.
  get floor(): Entity {
    if (!this.entity.has("VelocityComponent")) return Entity.NONE;
    return new Entity(VFI.bits(this.entity), VFG.bits(this.entity));
  }
  get platformVelocityX(): f32 { return VPX.get(this.entity); }
  get platformVelocityY(): f32 { return VPY.get(this.entity); }
}

const SR = new Field("SpriteComponent", "r");
const SG = new Field("SpriteComponent", "g");
const SB = new Field("SpriteComponent", "b");
const SA = new Field("SpriteComponent", "a");

// Options are authored as an object literal: sprite.shadow({ x: 12, y: -20 }).
export class ShadowOptions {
  x: f32 = 8;
  y: f32 = -8;
  scale: f32 = 1;
  layer: f32 = NaN; // omitted: just behind the owner's current z
  r: f32 = 0;
  g: f32 = 0;
  b: f32 = 0;
  alpha: f32 = 0.3;
}
const SHX = new Field("SpriteComponent", "shadowX");
const SHY = new Field("SpriteComponent", "shadowY");
const SHS = new Field("SpriteComponent", "shadowScale");
const SHL = new Field("SpriteComponent", "shadowLayer");
const SHR = new Field("SpriteComponent", "shadowR");
const SHG = new Field("SpriteComponent", "shadowG");
const SHB = new Field("SpriteComponent", "shadowB");
const SHA = new Field("SpriteComponent", "shadowAlpha");

// Tint (multiplied with the texture) and flipbook animations.
export class Sprite {
  constructor(readonly entity: Entity) {}
  get r(): f32 { return SR.get(this.entity); }
  set r(v: f32) { SR.set(this.entity, v); }
  get g(): f32 { return SG.get(this.entity); }
  set g(v: f32) { SG.set(this.entity, v); }
  get b(): f32 { return SB.get(this.entity); }
  set b(v: f32) { SB.set(this.entity, v); }
  get alpha(): f32 { return SA.get(this.entity); }
  set alpha(v: f32) { SA.set(this.entity, v); }

  setColor(r: f32, g: f32, b: f32, alpha: f32 = 1): void {
    this.r = r; this.g = g; this.b = b; this.alpha = alpha;
  }

  // The renderer follows position, rotation, scale, animation and opacity.
  // No entity, update hook or cleanup is needed. Offsets are world-space units.
  shadow(options: ShadowOptions = new ShadowOptions()): void {
    SHX.set(this.entity, options.x); SHY.set(this.entity, options.y);
    SHS.set(this.entity, max(0, options.scale)); SHL.set(this.entity, options.layer);
    SHR.set(this.entity, options.r); SHG.set(this.entity, options.g); SHB.set(this.entity, options.b);
    SHA.set(this.entity, max(0, min(1, options.alpha)));
  }
  clearShadow(): void { SHA.set(this.entity, 0); }

  // Switches to a SpriteAnimationComponent animation, leaving it running if it
  // already is; false if unknown.
  play(animation: string): bool { return this.start(animation, false); }
  // Plays an animation from its first frame, even if it is already running.
  restart(animation: string): bool { return this.start(animation, true); }
  // The animation playing, or "" (none, or a texture was set).
  get animation(): string {
    let n = __jmSpriteAnimation(this.entity.index, this.entity.generation, buf(), cap());
    if (grow(n)) n = __jmSpriteAnimation(this.entity.index, this.entity.generation, buf(), cap());
    return text(n, "");
  }

  // Shows an image ("assets/hud.png", "sprites.atlas.json#coin") from the next
  // frame, stopping any animation.
  setTexture(image: string): void {
    const i = utf8(image);
    __jmSpriteSetTexture(this.entity.index, this.entity.generation, i.dataStart, i.length);
  }

  private start(animation: string, restart: bool): bool {
    const a = utf8(animation);
    return __jmSpritePlay(this.entity.index, this.entity.generation, a.dataStart, a.length, restart);
  }
  // True once a non-looping animation shows its last frame.
  get finished(): bool { return __jmSpriteFinished(this.entity.index, this.entity.generation); }
}

const CHW = new Field("BoxColliderComponent", "halfWidth");
const CHH = new Field("BoxColliderComponent", "halfHeight");
const COX = new Field("BoxColliderComponent", "offsetX");
const COY = new Field("BoxColliderComponent", "offsetY");
const CLM = new Field("BoxColliderComponent", "layerMask");
const CCM = new Field("BoxColliderComponent", "collidesWithMask");
const CBM = new Field("BoxColliderComponent", "blocksMask");

// Two colliders touch when one's layerMask overlaps the other's collidesWithMask.
export class Collider {
  constructor(readonly entity: Entity) {}
  get halfWidth(): f32 { return CHW.get(this.entity); }
  set halfWidth(v: f32) { CHW.set(this.entity, v); }
  get halfHeight(): f32 { return CHH.get(this.entity); }
  set halfHeight(v: f32) { CHH.set(this.entity, v); }
  get offsetX(): f32 { return COX.get(this.entity); }
  set offsetX(v: f32) { COX.set(this.entity, v); }
  get offsetY(): f32 { return COY.get(this.entity); }
  set offsetY(v: f32) { COY.set(this.entity, v); }
  get layerMask(): u32 { return CLM.bits(this.entity); }
  set layerMask(v: u32) { CLM.setBits(this.entity, v); }
  get collidesWithMask(): u32 { return CCM.bits(this.entity); }
  set collidesWithMask(v: u32) { CCM.setBits(this.entity, v); }
  // Layers it's solid to: entities on them stop at it when they move() (0: none).
  get blocksMask(): u32 { return CBM.bits(this.entity); }
  set blocksMask(v: u32) { CBM.setBits(this.entity, v); }
  // Solid to every layer, or to none (e.g. a door opening).
  get solid(): bool { return this.blocksMask != 0; }
  set solid(v: bool) { this.blocksMask = v ? 0xFFFFFFFF : 0; }
}

const CR = new Field("CircleColliderComponent", "radius");
const CRX = new Field("CircleColliderComponent", "offsetX");
const CRY = new Field("CircleColliderComponent", "offsetY");
const CRL = new Field("CircleColliderComponent", "layerMask");
const CRC = new Field("CircleColliderComponent", "collidesWithMask");

// A round collider (CircleColliderComponent): touches as a Collider does, never solid.
export class CircleCollider {
  constructor(readonly entity: Entity) {}
  get radius(): f32 { return CR.get(this.entity); }
  set radius(v: f32) { CR.set(this.entity, v); }
  get offsetX(): f32 { return CRX.get(this.entity); }
  set offsetX(v: f32) { CRX.set(this.entity, v); }
  get offsetY(): f32 { return CRY.get(this.entity); }
  set offsetY(v: f32) { CRY.set(this.entity, v); }
  get layerMask(): u32 { return CRL.bits(this.entity); }
  set layerMask(v: u32) { CRL.setBits(this.entity, v); }
  get collidesWithMask(): u32 { return CRC.bits(this.entity); }
  set collidesWithMask(v: u32) { CRC.setBits(this.entity, v); }
}

const PR = new Field("ParticleEmitterComponent", "rate");
const PE = new Field("ParticleEmitterComponent", "emitting");
const PB = new Field("ParticleEmitterComponent", "burst");
const PA = new Field("ParticleEmitterComponent", "angle");
const PL = new Field("ParticleEmitterComponent", "alive");

// ParticleEmitterComponent: sparks, dust, smoke sent out from the entity.
export class Particles {
  constructor(readonly entity: Entity) {}
  // Sends out n at the next step (on top of any stream). On an entity spawned
  // this frame, it replaces the prefab's own burst: use that one at spawn.
  burst(n: u32): void { PB.setBits(this.entity, PB.bits(this.entity) + n); }
  get rate(): f32 { return PR.get(this.entity); }
  set rate(perSecond: f32) { PR.set(this.entity, perSecond); }
  get emitting(): bool { return PE.bits(this.entity) != 0; }
  set emitting(on: bool) { PE.setBits(this.entity, on ? 1 : 0); }
  // Degrees, the way they go (90: up).
  get angle(): f32 { return PA.get(this.entity); }
  set angle(degrees: f32) { PA.set(this.entity, degrees); }
  // How many are out now.
  get alive(): u32 { return PL.bits(this.entity); }
}

const LS = new Field("LifetimeComponent", "seconds");

// Seconds left before the entity is destroyed automatically.
export class Lifetime {
  constructor(readonly entity: Entity) {}
  get seconds(): f32 { return LS.get(this.entity); }
  set seconds(v: f32) { LS.set(this.entity, v); }
}

const XS = new Field("TextComponent", "size");
const XR = new Field("TextComponent", "r");
const XG = new Field("TextComponent", "g");
const XB = new Field("TextComponent", "b");
const XA = new Field("TextComponent", "a");

// A TextComponent: a line of text at the entity's position in the world (damage
// numbers, score popups), drawn over the sprites.
export class Text {
  constructor(readonly entity: Entity) {}
  set(text: string): void {
    const t = utf8(text);
    __jmTextSet(this.entity.index, this.entity.generation, t.dataStart, t.length);
  }
  get size(): f32 { return XS.get(this.entity); }
  set size(v: f32) { XS.set(this.entity, v); }
  get alpha(): f32 { return XA.get(this.entity); }
  set alpha(v: f32) { XA.set(this.entity, v); }
  setColor(r: f32, g: f32, b: f32, alpha: f32 = 1): void {
    XR.set(this.entity, r); XG.set(this.entity, g); XB.set(this.entity, b); XA.set(this.entity, alpha);
  }
}
