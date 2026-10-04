import {
  __jmRendererAddBuiltin,
  __jmRendererRemoveEffect,
  __jmRendererSetEffectEnabled,
  __jmRendererSetEffectUniformFloat,
  __jmRendererSetEffectUniformVec3,
  __jmRendererEffectCount,
  __jmRendererAddCustom,
  __jmRendererSetEffectUniformVec4,
} from "./env";
import { utf8 } from "./util";

export enum BuiltinEffect {
  Passthrough,
  Grayscale,
  Blur,
  Pixelate,
  ColorShift,
  Crossfade,
  Vignette,   // uniform u_strength (0..1, default 0.6)
  Flash,      // uniforms u_color (vec3), u_amount (0..1)
}

export class PostEffect {
  private _handle: u32 = 0;

  // Effects are appended to the end of the chain and belong to the current
  // scene: they are removed automatically when the scene unloads.
  // `customShader` (internal; use PostEffect.custom) replaces the builtin.
  constructor(id: BuiltinEffect, customShader: string = "") {
    if (customShader.length > 0) {
      const p = utf8(customShader);
      this._handle = <u32>__jmRendererAddCustom(<i32>p.dataStart, p.length - 1);
    } else {
      this._handle = <u32>__jmRendererAddBuiltin(<i32>id);
    }
  }

  // A custom fragment shader asset (".frag", listed in the manifest assets).
  // See the engine README for the uniforms every shader receives.
  static custom(shaderPath: string): PostEffect {
    return new PostEffect(BuiltinEffect.Passthrough, shaderPath);
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
    const view = utf8(name);
    __jmRendererSetEffectUniformFloat(<i32>this._handle, <i32>view.dataStart, view.length - 1, value);
  }

  public setUniformVec4(name: string, x: f32, y: f32, z: f32, w: f32): void {
    if (this._handle === 0) return;
    const view = utf8(name);
    __jmRendererSetEffectUniformVec4(<i32>this._handle, <i32>view.dataStart, view.length - 1, x, y, z, w);
  }

  public setUniformVec3(name: string, x: f32, y: f32, z: f32): void {
    if (this._handle === 0) return;
    const view = utf8(name);
    __jmRendererSetEffectUniformVec3(<i32>this._handle, <i32>view.dataStart, view.length - 1, x, y, z);
  }
};

export class PostEffects {
  public static count(): i32 {
    return __jmRendererEffectCount();
  }
};
