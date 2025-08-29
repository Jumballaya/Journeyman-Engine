
enum ComponentType {
  Transform,
  Sprite
};

class Component {
  constructor(public readonly type: string) { }
}

class TransformComponent extends Component {

  constructor(private b: Float32Array) {
    super("transform");
  }

  get x(): f32 { return this.b[0]; }
  get y(): f32 { return this.b[1]; }
  get z(): f32 { return this.b[2]; }
  set x(v: f32) { this.b[0] = v; }
  set y(v: f32) { this.b[1] = v; }
  set z(v: f32) { this.b[2] = v; }

  get sx(): f32 { return this.b[3]; }
  get sy(): f32 { return this.b[4]; }
  set sx(v: f32) { this.b[3] = v; }
  set sy(v: f32) { this.b[4] = v; }

  get rotation(): f32 { return this.b[5]; }
  set rotation(v: f32) { this.b[5] = v; }

};


class SpriteComponent extends Component {

  constructor(private _b: Float32Array) {
    super("sprite");
  }

  get r(): f32 { return this._b[0]; }
  get g(): f32 { return this._b[1]; }
  get b(): f32 { return this._b[2]; }
  get a(): f32 { return this._b[3]; }

  set r(v: f32) { this._b[0] = v; }
  set g(v: f32) { this._b[1] = v; }
  set b(v: f32) { this._b[2] = v; }
  set a(v: f32) { this._b[3] = v; }

  get tx(): f32 { return this._b[4]; }
  get ty(): f32 { return this._b[5]; }
  get tu(): f32 { return this._b[6]; }
  get tv(): f32 { return this._b[7]; }

  set tx(v: f32) { this._b[4] = v; }
  set ty(v: f32) { this._b[5] = v; }
  set tu(v: f32) { this._b[6] = v; }
  set tv(v: f32) { this._b[7] = v; }

  get layer(): f32 { return this._b[8]; }
  set layer(v: f32) { this._b[8] = v; }
};

class ECS {

  // ----
  // Components
  //

  // Names
  private static NAME_TRANS_BUF: ArrayBuffer = String.UTF8.encode("TransformComponent", true);
  private static NAME_TRANS_U8: Uint8Array = Uint8Array.wrap(ECS.NAME_TRANS_BUF);
  private static NAME_SPRITE_BUF: ArrayBuffer = String.UTF8.encode("SpriteComponent", true);
  private static NAME_SPRITE_U8: Uint8Array = Uint8Array.wrap(ECS.NAME_SPRITE_BUF);

  // POD Sizes
  private static SIZE_TRANS: i32 = 24;
  private static SIZE_SPRITE: i32 = 36;

  // POD Buffers
  private static POD_TRANS: Float32Array = new Float32Array(ECS.SIZE_TRANS / 4);
  private static POD_SPRITE: Float32Array = new Float32Array(ECS.SIZE_SPRITE / 4);

  // Component Classes
  private static COMP_TRANS: TransformComponent = new TransformComponent(ECS.POD_TRANS);
  private static COMP_SPRITE: SpriteComponent = new SpriteComponent(ECS.POD_SPRITE);

  // ----



  public static getComponent(type: ComponentType): Component | null {
    if (type == ComponentType.Transform) {
      ECS.getTransform();
      return ECS.COMP_TRANS;
    }
    if (type == ComponentType.Sprite) {
      ECS.getSprite();
      return ECS.COMP_SPRITE;
    }
    return null;
  }

  public static updateComponent(type: ComponentType, comp: Component): void {
    if (type == ComponentType.Transform && comp.type === "transform") {
      ECS.updateTransform(comp as TransformComponent);
      return;
    }
    if (type == ComponentType.Sprite && comp.type === "sprite") {
      ECS.updateSprite(comp as SpriteComponent);
      return ECS.COMP_SPRITE;
    }
  }


  private static getTransform(): void {
    __jmEcsGetComponent(
      ECS.NAME_TRANS_U8.dataStart, ECS.NAME_TRANS_U8.length - 1,
      ECS.POD_TRANS.dataStart, ECS.POD_TRANS.byteLength
    );
  }

  private static getSprite(): void {
    __jmEcsGetComponent(
      ECS.NAME_SPRITE_U8.dataStart, ECS.NAME_SPRITE_U8.length - 1,
      ECS.POD_SPRITE.dataStart, ECS.POD_SPRITE.byteLength
    );
  }


  private static updateTransform(transform: TransformComponent): void {
    __jmEcsUpdateComponent(
      ECS.NAME_TRANS_U8.dataStart, ECS.NAME_TRANS_U8.length - 1,
      ECS.POD_TRANS.dataStart
    );
  }

  private static updateSprite(sprite: SpriteComponent): void {
    __jmEcsUpdateComponent(
      ECS.NAME_SPRITE_U8.dataStart, ECS.NAME_SPRITE_U8.length - 1,
      ECS.POD_SPRITE.dataStart
    );
  }
}