import { __jmSceneLoad, __jmSceneTransition, __jmSceneIsTransitioning, __jmSceneCurrent } from "./env";
import { utf8, buf, cap, grow, text } from "./util";

// Scenes by name ("level2") or path. Changes apply at the end of the frame
// and destroy everything from the previous scene.
export class Scene {
  static load(scene: string): void {
    const s = utf8(scene);
    __jmSceneLoad(s.dataStart, s.length);
  }

  // Blends into the next scene over `seconds`: a crossfade, or a transition
  // shader by name ("wipe"). Ignored while another transition runs.
  static transition(scene: string, seconds: f32 = 0.5, shader: string = ""): void {
    const s = utf8(scene);
    const sh = utf8(shader);
    __jmSceneTransition(s.dataStart, s.length, seconds, sh.dataStart, sh.length);
  }

  static get transitioning(): bool { return __jmSceneIsTransitioning(); }

  // Path of the active scene, e.g. "scenes/level1.scene.json".
  static get current(): string {
    let n = __jmSceneCurrent(buf(), cap());
    if (grow(n)) n = __jmSceneCurrent(buf(), cap());
    return text(n, "");
  }
}
