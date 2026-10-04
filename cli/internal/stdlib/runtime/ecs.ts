import {
  __jmEcsGetComponent, __jmEcsUpdateComponent,
  __jmEcsGetComponentOf, __jmEcsUpdateComponentOf,
} from "./env";
import { Entity } from "./entity";
import { utf8 } from "./util";

// A script-side copy of one engine component. `read(e)` pulls the entity's
// current values into this object, `write(e)` pushes them back. Both default
// to the script's own entity. Keep one instance around and reuse it — they
// are cheap to read/write and allocation-free after construction.
export abstract class Component {
  protected readonly bytes: Uint8Array;
  protected readonly f: Float32Array;
  private readonly nameBytes: Uint8Array;

  constructor(public readonly name: string, podSize: i32) {
    const bytes = new Uint8Array(podSize);
    this.bytes = bytes;
    this.f = Float32Array.wrap(bytes.buffer);
    this.nameBytes = utf8(name);
  }

  // Returns false if the entity is dead or lacks this component.
  read(entity: Entity | null = null): bool {
    const n = this.nameBytes;
    const r = entity === null
      ? __jmEcsGetComponent(<i32>n.dataStart, n.length - 1, <i32>this.bytes.dataStart, this.bytes.length)
      : __jmEcsGetComponentOf(<i32>entity.index, <i32>entity.generation, <i32>n.dataStart, n.length - 1,
                              <i32>this.bytes.dataStart, this.bytes.length);
    return r > 0;
  }

  write(entity: Entity | null = null): bool {
    const n = this.nameBytes;
    const r = entity === null
      ? __jmEcsUpdateComponent(<i32>n.dataStart, n.length - 1, <i32>this.bytes.dataStart)
      : __jmEcsUpdateComponentOf(<i32>entity.index, <i32>entity.generation, <i32>n.dataStart, n.length - 1,
                                 <i32>this.bytes.dataStart);
    return r > 0;
  }
}

// Position is the entity's center in world units; scale is the HALF size
// (sprite quads span -1..1). z orders drawing: higher z draws on top.
export class TransformComponent extends Component {
  constructor() { super("TransformComponent", 24); }
  get x(): f32 { return this.f[0]; }
  set x(v: f32) { this.f[0] = v; }
  get y(): f32 { return this.f[1]; }
  set y(v: f32) { this.f[1] = v; }
  get z(): f32 { return this.f[2]; }
  set z(v: f32) { this.f[2] = v; }
  get sx(): f32 { return this.f[3]; }
  set sx(v: f32) { this.f[3] = v; }
  get sy(): f32 { return this.f[4]; }
  set sy(v: f32) { this.f[4] = v; }
  get rotation(): f32 { return this.f[5]; }
  set rotation(v: f32) { this.f[5] = v; }
}

export class SpriteComponent extends Component {
  constructor() { super("SpriteComponent", 36); }
  get r(): f32 { return this.f[0]; }
  set r(v: f32) { this.f[0] = v; }
  get g(): f32 { return this.f[1]; }
  set g(v: f32) { this.f[1] = v; }
  get b(): f32 { return this.f[2]; }
  set b(v: f32) { this.f[2] = v; }
  get a(): f32 { return this.f[3]; }
  set a(v: f32) { this.f[3] = v; }
  get tx(): f32 { return this.f[4]; }
  set tx(v: f32) { this.f[4] = v; }
  get ty(): f32 { return this.f[5]; }
  set ty(v: f32) { this.f[5] = v; }
  get tu(): f32 { return this.f[6]; }
  set tu(v: f32) { this.f[6] = v; }
  get tv(): f32 { return this.f[7]; }
  set tv(v: f32) { this.f[7] = v; }
  get layer(): f32 { return this.f[8]; }
  set layer(v: f32) { this.f[8] = v; }

  setColor(r: f32, g: f32, b: f32, a: f32 = 1.0): void {
    this.r = r; this.g = g; this.b = b; this.a = a;
  }
}

// Units per second; MovementSystem integrates it into the transform.
export class VelocityComponent extends Component {
  constructor() { super("VelocityComponent", 8); }
  get vx(): f32 { return this.f[0]; }
  set vx(v: f32) { this.f[0] = v; }
  get vy(): f32 { return this.f[1]; }
  set vy(v: f32) { this.f[1] = v; }
}

export class BoxColliderComponent extends Component {
  private readonly u: Uint32Array;
  constructor() {
    super("BoxColliderComponent", 24);
    this.u = Uint32Array.wrap(this.f.buffer);
  }
  get halfWidth(): f32 { return this.f[0]; }
  set halfWidth(v: f32) { this.f[0] = v; }
  get halfHeight(): f32 { return this.f[1]; }
  set halfHeight(v: f32) { this.f[1] = v; }
  get offsetX(): f32 { return this.f[2]; }
  set offsetX(v: f32) { this.f[2] = v; }
  get offsetY(): f32 { return this.f[3]; }
  set offsetY(v: f32) { this.f[3] = v; }
  get layerMask(): u32 { return this.u[4]; }
  set layerMask(v: u32) { this.u[4] = v; }
  get collidesWithMask(): u32 { return this.u[5]; }
  set collidesWithMask(v: u32) { this.u[5] = v; }
}

// Seconds until the entity is destroyed automatically.
export class LifetimeComponent extends Component {
  constructor() { super("LifetimeComponent", 4); }
  get seconds(): f32 { return this.f[0]; }
  set seconds(v: f32) { this.f[0] = v; }
}

// ---- Legacy API (kept for existing scripts) --------------------------------

export enum ComponentType {
  Transform,
  Sprite,
}

export class ECS {
  private static TRANSFORM: TransformComponent = new TransformComponent();
  private static SPRITE: SpriteComponent = new SpriteComponent();

  // Reads the script entity's component into a shared instance.
  public static getComponent(type: ComponentType): Component | null {
    const c: Component = type == ComponentType.Transform ? ECS.TRANSFORM : ECS.SPRITE;
    return c.read() ? c : null;
  }

  public static updateComponent(type: ComponentType, comp: Component): void {
    comp.write();
  }
}
