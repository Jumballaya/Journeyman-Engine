import {
  __jmCameraShake, __jmCameraSetPosition, __jmRendererSetClearColor,
  __jmWindowSetFullscreen, __jmWindowIsFullscreen, __jmWindowIsFocused,
} from "./env";

export class Camera {
  // Shakes the view by up to `amplitude` world units, fading over `duration`
  // seconds. Overlapping requests keep the strongest.
  static shake(amplitude: f32, duration: f32): void {
    __jmCameraShake(amplitude, duration);
  }

  // Center of the view in world units (default 0, 0). Reset on scene change.
  static setPosition(x: f32, y: f32): void {
    __jmCameraSetPosition(x, y);
  }
}

export class Renderer {
  // Background color behind all sprites (0..1 components).
  static setClearColor(r: f32, g: f32, b: f32, a: f32 = 1.0): void {
    __jmRendererSetClearColor(r, g, b, a);
  }
}

export class Window {
  static get fullscreen(): bool { return __jmWindowIsFullscreen() != 0; }
  static set fullscreen(on: bool) { __jmWindowSetFullscreen(on ? 1 : 0); }
  // False while another app has focus — a good moment to auto-pause.
  static get focused(): bool { return __jmWindowIsFocused() != 0; }
}
