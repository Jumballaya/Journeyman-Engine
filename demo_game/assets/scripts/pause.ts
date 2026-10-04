// Pause overlay. Runs while the game clock is paused (runWhenPaused).
import { GameState, Input, Time, Scene, App, Audio, Bus, Window } from "@jm/runtime";
import {
  Menu, setVisible, sfx, musicVolume, transition, restoreStageSnapshot, commitHiscore,
  SCENE_TITLE, SHADER_DISSOLVE,
} from "./lib/game";

const menu = new Menu(["p-resume", "p-restart", "p-menu", "p-quit"]);
let open = false;

function canPause(): bool {
  return GameState.getNumber("stageOver") == 0 && GameState.getNumber("gameOver") == 0 && !Scene.isTransitioning();
}

function show(): void {
  open = true;
  Time.pause();
  menu.index = 0;
  menu.render();
  setVisible("pause", true);
  Audio.setVolume(Bus.Music, musicVolume() * 0.3);
  sfx("menu_select", 0.7);
}

function hide(): void {
  open = false;
  Time.resume();
  setVisible("pause", false);
  Audio.setVolume(Bus.Music, musicVolume());
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
  if (choice == 0) {
    hide();
  } else if (choice == 1) {
    Audio.setVolume(Bus.Music, musicVolume());
    restoreStageSnapshot();
    transition(Scene.current(), SHADER_DISSOLVE, 0.8);
  } else if (choice == 2) {
    Audio.setVolume(Bus.Music, musicVolume());
    commitHiscore();
    transition(SCENE_TITLE, SHADER_DISSOLVE, 0.8);
  } else if (choice == 3) {
    commitHiscore();
    App.quit();
  }
}
