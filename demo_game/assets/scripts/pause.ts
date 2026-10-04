// Pause overlay. Runs while the game clock is paused (runWhenPaused).
import { App, Audio, Bus, Input, Scene, Time, Window } from "@jm/runtime";
import { sfx } from "./lib/util";
import { Session, recordHiscore } from "./lib/session";
import { Settings } from "./lib/settings";
import { Menu, goTo, setVisible } from "./lib/screens";

const menu = new Menu(["p-resume", "p-restart", "p-menu", "p-quit"]);
let open = false;

function canPause(): bool {
  return !Session.stageOver && !Session.gameOver && !Scene.transitioning;
}

function show(): void {
  open = true;
  Time.pause();
  menu.select(0);
  setVisible("pause", true);
  Audio.setVolume(Bus.Music, Settings.musicVolume * 0.3);
  sfx("menu_select", 0.7);
}

function hide(): void {
  open = false;
  Time.resume();
  setVisible("pause", false);
  Settings.apply();
}

export function onUpdate(dt: f32): void {
  if (!open) {
    // Pause on request, or automatically when the player switches away.
    if ((Input.pressed("pause") || !Window.focused) && canPause()) show();
    return;
  }
  if (Input.pressed("pause") || Input.pressed("back")) {
    sfx("menu_back", 0.7);
    hide();
    return;
  }
  const choice = menu.update();
  if (choice == "p-resume") {
    hide();
  } else if (choice == "p-restart") {
    Settings.apply();
    Session.restartStage();
    goTo(Scene.current, "dissolve", 0.8);
  } else if (choice == "p-menu") {
    Settings.apply();
    recordHiscore();
    goTo("title", "dissolve", 0.8);
  } else if (choice == "p-quit") {
    recordHiscore();
    App.quit();
  }
}
