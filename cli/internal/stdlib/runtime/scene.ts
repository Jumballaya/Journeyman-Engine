class Scene {
  public static load(scenePath: string): void {
    const utf8 = String.UTF8.encode(scenePath, true);
    const view = Uint8Array.wrap(utf8);
    __jmSceneLoad(view.dataStart, view.length - 1);
  }

  public static transition(scenePath: string, durationSeconds: f32 = 0.5): void {
    const utf8 = String.UTF8.encode(scenePath, true);
    const view = Uint8Array.wrap(utf8);
    __jmSceneTransition(view.dataStart, view.length - 1, durationSeconds);
  }

  public static isTransitioning(): boolean {
    return __jmSceneIsTransitioning() != 0;
  }
};
