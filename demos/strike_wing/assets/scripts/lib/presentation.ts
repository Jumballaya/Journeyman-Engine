// Strike Wing's presentation choices; jm owns persistence and screen setup.
import { Settings, Screen } from "@jm/runtime";

export const settings = new Settings({ musicVolume: 0.6, sfxVolume: 0.8 });
export const crt = settings.effect("crt", "crt").setFloat("u_strength", 1);
export const screen = new Screen({
  settings: settings, transition: "wipe", seconds: 1, hiddenClass: "hidden",
  menuMove: "menu_move", menuConfirm: "menu_select", moveGain: 0.6, confirmGain: 0.8,
});
