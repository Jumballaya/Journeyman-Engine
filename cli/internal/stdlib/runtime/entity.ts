import {
  __jmSelf, __jmEntityIsAlive, __jmEntityHasTag, __jmEntitySetTag, __jmEntityHasComponent,
  __jmWorldDestroy, __jmFieldId, __jmFieldGet, __jmFieldSet, __jmSpritePlay, __jmSpriteFinished,
  __jmSpriteAnimation, __jmSpriteSetTexture, __jmEntityStore, __jmEntitySend, __jmTextSet,
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
  // before its next update; dropped if it has no script.
  send(name: string, text: string = "", number: f64 = 0): void {
    const n = utf8(name);
    const t = utf8(text);
    __jmEntitySend(this.index, this.generation, n.dataStart, n.length, t.dataStart, t.length, number);
  }

  get transform(): Transform { return new Transform(this); }
  get velocity(): Velocity { return new Velocity(this); }
  get sprite(): Sprite { return new Sprite(this); }
  get collider(): Collider { return new Collider(this); }
  get lifetime(): Lifetime { return new Lifetime(this); }
  get text(): Text { return new Text(this); }

  private setTag(tag: string, present: bool): void {
    const t = utf8(tag);
    __jmEntitySetTag(this.index, this.generation, t.dataStart, t.length, present);
  }
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

const VX = new Field("VelocityComponent", "vx");
const VY = new Field("VelocityComponent", "vy");
const VAX = new Field("VelocityComponent", "ax");
const VAY = new Field("VelocityComponent", "ay");

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
