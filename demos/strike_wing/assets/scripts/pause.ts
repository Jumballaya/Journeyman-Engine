// Pause overlay. Runs while the game clock is paused (runWhenPaused).
import { settings, screen } from "./lib/presentation";
import { App, Audio, Bus, Input, Scene, Time, Window } from "@jm/runtime";
import * as Session from "./lib/session";
import { recordHiscore } from "./lib/session";

const menu = screen.menu(["p-resume", "p-restart", "p-menu", "p-quit"]);
let open = false;

function canPause(): bool {
  return !Session.stageOver.value && !Session.gameOver.value && !Scene.transitioning;
}

function show(): void {
  open = true;
  Time.pause();
  menu.select(0);
  screen.setVisible("pause", true);
  Audio.setVolume(Bus.Music, settings.musicVolume * 0.3);
  Audio.play("menu_select", 0.7);
}

function hide(): void {
  open = false;
  Time.resume();
  screen.setVisible("pause", false);
  settings.apply();
}

export function onUpdate(dt: f32): void {
  if (!open) {
    // Pause on request, or automatically when the player switches away.
    if ((Input.justPressed("pause") || !Window.focused) && canPause()) show();
    return;
  }
  if (Input.justPressed("pause") || Input.justPressed("back")) {
    Audio.play("menu_back", 0.7);
    hide();
    return;
  }
  const choice = menu.update();
  if (choice == "p-resume") {
    hide();
  } else if (choice == "p-restart") {
    settings.apply();
    Session.restartStage();
    screen.goTo(Scene.current, "dissolve", 0.8);
  } else if (choice == "p-menu") {
    settings.apply();
    recordHiscore();
    screen.goTo("title", "dissolve", 0.8);
  } else if (choice == "p-quit") {
    recordHiscore();
    App.quit();
  }
}
