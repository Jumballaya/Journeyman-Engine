import {
  __jmSceneLoad, __jmSceneTransition, __jmSceneTransitionWith, __jmSceneIsTransitioning, __jmSceneCurrent,
} from "./env";
import { utf8, scratchPtr, scratchCap, needsRetry, scratchString } from "./util";

export class Scene {
  // Immediate swap at the end of the frame.
  public static load(scenePath: string): void {
    const view = utf8(scenePath);
    __jmSceneLoad(<i32>view.dataStart, view.length - 1);
  }

  // Shader-composited swap. `shader` is an optional .frag asset path; it
  // receives the old frame as u_aux, the new as u_primary, and u_progress
  // 0 → 1. Empty = crossfade. Ignored while another transition is running.
  public static transition(scenePath: string, durationSeconds: f32 = 0.5, shader: string = ""): void {
    const view = utf8(scenePath);
    if (shader.length == 0) {
      __jmSceneTransition(<i32>view.dataStart, view.length - 1, durationSeconds);
      return;
    }
    const s = utf8(shader);
    __jmSceneTransitionWith(<i32>view.dataStart, view.length - 1, durationSeconds, <i32>s.dataStart, s.length - 1);
  }

  public static isTransitioning(): boolean {
    return __jmSceneIsTransitioning() != 0;
  }

  // Path of the active scene, e.g. "scenes/level1.scene.json".
  public static current(): string {
    let n = __jmSceneCurrent(scratchPtr(), scratchCap());
    if (needsRetry(n)) n = __jmSceneCurrent(scratchPtr(), scratchCap());
    return n < 0 ? "" : scratchString(n);
  }
};
