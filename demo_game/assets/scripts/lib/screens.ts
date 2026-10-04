// Strike Wing's menu sounds, visibility convention and scene transitions.
import { Menu, Sound, Scene, UI } from "@jm/runtime";

export function setVisible(id: string, visible: bool): void { UI.setVisible(id, visible, "hidden"); }
export function goTo(scene: string, shader: string = "wipe", seconds: f32 = 1): void {
  if (!Scene.transitioning) Scene.transition(scene, seconds, shader);
}
export function gameMenu(items: string[]): Menu {
  return new Menu(items).sounds(new Sound("menu_move"), new Sound("menu_select"), 0.6, 0.8);
}
