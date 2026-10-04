import { Menu } from "./menu";
import { Sound } from "./audio";
import { Scene } from "./scene";
import { Settings } from "./settings";
import { UI } from "./ui";

export class ScreenOptions {
  transition: string = "";
  seconds: f32 = 0.5;
  hiddenClass: string = "";
  menuMove: string = "";
  menuConfirm: string = "";
  moveGain: f32 = 1;
  confirmGain: f32 = 1;
  settings: Settings | null = null;
}

// Shared presentation defaults. open/update belong to the scene's controller;
// overlays can use menus and visibility without opening another set of effects.
export class Screen {
  constructor(private options: ScreenOptions = new ScreenOptions()) {}
  open(): void { const settings = this.options.settings; if (settings !== null) settings.open(); }
  update(): void { const settings = this.options.settings; if (settings !== null) settings.update(); }
  menu(items: string[]): Menu {
    const menu = new Menu(items);
    if (this.options.menuMove.length > 0 && this.options.menuConfirm.length > 0)
      menu.sounds(new Sound(this.options.menuMove), new Sound(this.options.menuConfirm), this.options.moveGain, this.options.confirmGain);
    return menu;
  }
  setVisible(id: string, visible: bool): void { UI.setVisible(id, visible, this.options.hiddenClass); }
  panels(ids: string[]): Panels { return new Panels(ids, this.options.hiddenClass); }
  goTo(scene: string, shader: string = this.options.transition, seconds: f32 = this.options.seconds): void {
    if (!Scene.transitioning) Scene.transition(scene, seconds, shader);
  }
}

// One visible panel at a time. An empty ID hides all panels.
export class Panels {
  private ids: string[];
  private current: string = "";
  constructor(ids: string[], private hiddenClass: string = "") { this.ids = ids.slice(); }
  get active(): string { return this.current; }
  show(id: string): void {
    assert(id == "" || this.ids.includes(id), "Unknown panel");
    this.current = id;
    for (let i = 0; i < this.ids.length; i++) UI.setVisible(this.ids[i], this.ids[i] == id, this.hiddenClass);
  }
}
