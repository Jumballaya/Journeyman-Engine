import {
  __jmEffectAddBuiltin, __jmEffectAddCustom, __jmEffectRemove, __jmEffectSetEnabled, __jmEffectSetUniform,
  __jmCameraShake, __jmCameraSetPosition, __jmCameraSetZoom, __jmCameraView, __jmRendererSetClearColor,
  __jmWindowSetFullscreen, __jmWindowIsFullscreen, __jmWindowIsFocused,
} from "./env";
import { utf8 } from "./util";
import { Vec2 } from "./math";

// A full-screen shader applied after the scene is drawn, in the order added.
// Effects belong to the current scene and are removed when it unloads.
export class PostEffect {
  private constructor(private id: u32) {}

  // Engine effects: "grayscale", "blur" (u_radius), "pixelate" (u_pixelSize),
  // "colorshift" (u_hsvDelta), "vignette" (u_strength), "flash" (u_color, u_amount).
  static builtin(name: string): PostEffect {
    const n = utf8(name);
    return new PostEffect(__jmEffectAddBuiltin(n.dataStart, n.length));
  }

  // Your own .frag shader by name ("crt") or path; see docs/content.md.
  static custom(shader: string): PostEffect {
    const s = utf8(shader);
    return new PostEffect(__jmEffectAddCustom(s.dataStart, s.length));
  }

  setFloat(name: string, x: f32): PostEffect { return this.uniform(name, 1, x, 0, 0, 0); }
  setVec2(name: string, x: f32, y: f32): PostEffect { return this.uniform(name, 2, x, y, 0, 0); }
  setVec3(name: string, x: f32, y: f32, z: f32): PostEffect { return this.uniform(name, 3, x, y, z, 0); }
  setVec4(name: string, x: f32, y: f32, z: f32, w: f32): PostEffect { return this.uniform(name, 4, x, y, z, w); }

  set enabled(on: bool) { __jmEffectSetEnabled(this.id, on); }

  remove(): void {
    __jmEffectRemove(this.id);
    this.id = 0;
  }

  private uniform(name: string, count: i32, x: f32, y: f32, z: f32, w: f32): PostEffect {
    const n = utf8(name);
    __jmEffectSetUniform(this.id, n.dataStart, n.length, count, x, y, z, w);
    return this;
  }
}

const view = new StaticArray<f32>(5);  // center x, y, half width, half height, zoom

function readView(): void { __jmCameraView(changetype<usize>(view), 20); }

// The view onto the world. Reset when a scene loads.
export class Camera {
  // Shakes by up to `amplitude` world units, fading over `seconds`; the
  // strongest overlapping shake wins.
  static shake(amplitude: f32, seconds: f32): void { __jmCameraShake(amplitude, seconds); }
  // Center of the view in world units (default 0, 0).
  static setPosition(x: f32, y: f32): void { __jmCameraSetPosition(x, y); }
  static get x(): f32 { readView(); return view[0]; }
  static get y(): f32 { readView(); return view[1]; }
  // Above 1 zooms in (2: everything twice as big), below 1 out; default 1.
  static get zoom(): f32 { readView(); return view[4]; }
  static set zoom(zoom: f32) { __jmCameraSetZoom(zoom); }
  // How much of the world the view shows, in world units.
  static get width(): f32 { readView(); return view[2] * 2; }
  static get height(): f32 { readView(); return view[3] * 2; }

  // Screen (UI) pixels, y down from the top-left, to world units, y up.
  static toWorld(screenX: f32, screenY: f32, out: Vec2): Vec2 {
    readView();
    return out.set(view[0] - view[2] + screenX / view[4], view[1] + view[3] - screenY / view[4]);
  }
  static toScreen(worldX: f32, worldY: f32, out: Vec2): Vec2 {
    readView();
    return out.set((worldX - view[0] + view[2]) * view[4], (view[1] + view[3] - worldY) * view[4]);
  }
}

export class Renderer {
  // Background behind all sprites, 0..1 components.
  static setClearColor(r: f32, g: f32, b: f32, a: f32 = 1): void { __jmRendererSetClearColor(r, g, b, a); }
}

export class Window {
  static get fullscreen(): bool { return __jmWindowIsFullscreen(); }
  static set fullscreen(on: bool) { __jmWindowSetFullscreen(on); }
  // False while another app has focus: a good moment to auto-pause.
  static get focused(): bool { return __jmWindowIsFocused(); }
}
