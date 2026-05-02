import {
  __jmRendererAddBuiltin,
  __jmRendererRemoveEffect,
  __jmRendererSetEffectEnabled,
  __jmRendererSetEffectUniformFloat,
  __jmRendererSetEffectUniformVec3,
  __jmRendererEffectCount,
} from "./env";

export enum BuiltinEffect {
  Passthrough,
  Grayscale,
  Blur,
  Pixelate,
  ColorShift,
  Crossfade,
}

export class PostEffect {
  private _handle: u32 = 0;

  constructor(id: BuiltinEffect) {
    const raw = __jmRendererAddBuiltin(<i32>id);
    this._handle = <u32>raw;
  }

  public get handle(): u32 {
    return this._handle;
  }

  public get isValid(): boolean {
    return this._handle !== 0;
  }

  public remove(): void {
    if (this._handle !== 0) {
      __jmRendererRemoveEffect(<i32>this._handle);
      this._handle = 0;
    }
  }

  public setEnabled(enabled: boolean): void {
    if (this._handle !== 0) {
      __jmRendererSetEffectEnabled(<i32>this._handle, enabled ? 1 : 0);
    }
  }

  public setUniform(name: string, value: f32): void {
    if (this._handle === 0) return;
    const utf8 = String.UTF8.encode(name, true);
    const view = Uint8Array.wrap(utf8);
    __jmRendererSetEffectUniformFloat(<i32>this._handle, view.dataStart, view.length - 1, value);
  }

  public setUniformVec3(name: string, x: f32, y: f32, z: f32): void {
    if (this._handle === 0) return;
    const utf8 = String.UTF8.encode(name, true);
    const view = Uint8Array.wrap(utf8);
    __jmRendererSetEffectUniformVec3(<i32>this._handle, view.dataStart, view.length - 1, x, y, z);
  }
};

export class PostEffects {
  public static count(): i32 {
    return __jmRendererEffectCount();
  }
};
