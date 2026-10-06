import {
  __jmSceneLoad, __jmSceneTransition, __jmSceneIsTransitioning, __jmSceneCurrent,
  __jmSceneSpawnGroup, __jmSceneDespawnGroup, __jmSceneGroupSpawned,
} from "./env";
import { utf8, buf, cap, grow, text } from "./util";

// Scenes by name ("level2") or path. Changes apply at the end of the frame
// (after any running transition) and destroy everything from the previous
// scene. The latest request wins.
export class Scene {
  static load(scene: string): void {
    const s = utf8(scene);
    __jmSceneLoad(s.dataStart, s.length);
  }

  // Blends into the next scene over `seconds`: a crossfade, or a transition
  // shader by name ("wipe"). Waits for a running transition to finish.
  static transition(scene: string, seconds: f32 = 0.5, shader: string = ""): void {
    const s = utf8(scene);
    const sh = utf8(shader);
    __jmSceneTransition(s.dataStart, s.length, seconds, sh.dataStart, sh.length);
  }

  static get transitioning(): bool { return __jmSceneIsTransitioning(); }

  // Groups: entries the scene marks with "group" (a room, a wave) wait until
  // spawned. Spawning builds them (those whose "if"/"unless" hold); despawning
  // removes the ones still alive, so the next spawn starts the group afresh.
  // Both apply at the end of the frame.
  static spawnGroup(group: string): void {
    const g = utf8(group);
    __jmSceneSpawnGroup(g.dataStart, g.length);
  }

  static despawnGroup(group: string): void {
    const g = utf8(group);
    __jmSceneDespawnGroup(g.dataStart, g.length);
  }

  static groupSpawned(group: string): bool {
    const g = utf8(group);
    return __jmSceneGroupSpawned(g.dataStart, g.length);
  }

  // Path of the active scene, e.g. "scenes/level1.scene.json".
  static get current(): string {
    let n = __jmSceneCurrent(buf(), cap());
    if (grow(n)) n = __jmSceneCurrent(buf(), cap());
    return text(n, "");
  }
}
