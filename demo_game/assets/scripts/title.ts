// Title screen: main menu, how-to-play panel, options (volumes, CRT,
// fullscreen), and an attract-mode squadron flying past.
import { World, Save, Input, UI, Sound, Bus, App, Window, PostEffect } from "@jm/runtime";
import {
  Menu, setVisible, sfx, rand, randInt, pad, savedHiscore, applySettings, addCrt, handleGlobalKeys,
  musicVolume, sfxVolume, newGame, transition, stageScene, SHADER_WIPE,
} from "./lib/game";

const MAIN = 0, HOWTO = 1, OPTIONS = 2;

const mainMenu = new Menu(["m-start", "m-howto", "m-options", "m-quit"]);
const optionsMenu = new Menu(["o-music", "o-sfx", "o-crt", "o-fullscreen", "o-back"]);
const music = new Sound("assets/sounds/music_title.wav", Bus.Music);
let crt: PostEffect | null = null;
let screen = MAIN;
let started = false;
let leaving = false;
let t: f32 = 0;
let squadronTimer: f32 = 1.5;

function showScreen(s: i32): void {
  screen = s;
  setVisible("main-menu", s == MAIN);
  setVisible("howto", s == HOWTO);
  setVisible("options", s == OPTIONS);
  setVisible("prompt", s != HOWTO);
}

function renderOptions(): void {
  UI.setStyle("o-music-fill", "width", (<i32>(musicVolume() * 100)).toString() + "%");
  UI.setStyle("o-sfx-fill", "width", (<i32>(sfxVolume() * 100)).toString() + "%");
  UI.setText("o-crt-value", Save.getNumber("crt", 1) > 0 ? "ON" : "OFF");
  UI.setText("o-fullscreen-value", Window.fullscreen ? "ON" : "OFF");
  optionsMenu.render();
}

function adjustVolume(key: string, delta: f32): void {
  const v = Math.round(Math.min(1, Math.max(0, Save.getNumber(key, key == "musicVolume" ? 0.6 : 0.8) + delta)) * 10) / 10;
  Save.setNumber(key, v);
  applySettings();
  sfx("menu_move", 0.8);
}

function toggleCrt(): void {
  const on = Save.getNumber("crt", 1) == 0;
  Save.setNumber("crt", on ? 1 : 0);
  const fx = crt;
  if (fx !== null) fx.setEnabled(on);
}

function updateOptions(): void {
  const choice = optionsMenu.update();
  const item = optionsMenu.index;
  const dir: f32 = Input.pressed("right") ? 1 : Input.pressed("left") ? -1 : 0;
  if (item == 0 && dir != 0) adjustVolume("musicVolume", dir * 0.1);
  if (item == 1 && dir != 0) adjustVolume("sfxVolume", dir * 0.1);
  if ((item == 2 && (dir != 0 || choice == 2))) toggleCrt();
  if (item == 3 && (dir != 0 || choice == 3)) {
    const on = !Window.fullscreen;
    Window.fullscreen = on;
    Save.setNumber("fullscreen", on ? 1 : 0);
  }
  if (choice == 4 || Input.pressed("back")) {
    if (choice != 4) sfx("menu_back", 0.7);
    showScreen(MAIN);
    return;
  }
  renderOptions();
}

function spawnSquadron(): void {
  const ships = ["ship_0000", "ship_0004", "ship_0008", "ship_0001"];
  const ship = ships[randInt(0, ships.length - 1)];
  const cx = rand(-150, 150);
  const n = randInt(3, 5);
  for (let i = 0; i < n; i++) {
    const side: f32 = <f32>((i + 1) / 2) * (i % 2 == 0 ? 1 : -1);
    World.spawn("assets/prefabs/attract_plane.prefab.json", cx + side * 44, -380 - Mathf.abs(side) * 40,
      '{"SpriteComponent":{"texture":"assets/atlases/shmup.atlas.json#' + ship + '"}}');
  }
}

function start(): void {
  started = true;
  applySettings();
  if (Save.getNumber("fullscreen", 0) > 0 && !Window.fullscreen) Window.fullscreen = true;
  crt = addCrt();
  music.play(0.9, true);
  UI.setText("hiscore", pad(savedHiscore()));
  mainMenu.render();
  showScreen(MAIN);
}

export function onUpdate(dt: f32): void {
  if (!started) start();
  t += dt;
  handleGlobalKeys();

  squadronTimer -= dt;
  if (squadronTimer <= 0) {
    squadronTimer = rand(5, 9);
    spawnSquadron();
  }
  UI.setStyle("prompt", "opacity", Mathf.floor(t * 2) % 2 == 0 ? "1" : "0.4");
  if (leaving) return;

  if (screen == MAIN) {
    const choice = mainMenu.update();
    if (choice == 0) {
      leaving = true;
      newGame();
      music.fadeOut(0.8);
      transition(stageScene(1), SHADER_WIPE, 1.0);
    } else if (choice == 1) {
      showScreen(HOWTO);
    } else if (choice == 2) {
      optionsMenu.index = 0;
      renderOptions();
      showScreen(OPTIONS);
    } else if (choice == 3) {
      App.quit();
    }
  } else if (screen == HOWTO) {
    if (Input.pressed("confirm") || Input.pressed("back")) {
      sfx("menu_back", 0.7);
      showScreen(MAIN);
    }
  } else {
    updateOptions();
  }
}
