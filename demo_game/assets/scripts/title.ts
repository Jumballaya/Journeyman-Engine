// Title screen: main menu, how-to-play, options (volumes, CRT, fullscreen)
// and an attract-mode squadron flying past.
import { Timer, blink, Random, App, Input, Music, Overrides, UI, spawn } from "@jm/runtime";
import { scoreText, sfx } from "./lib/util";
import { Session, hiscore, stageScene } from "./lib/session";
import { Settings, addCrt, handleGlobalKeys } from "./lib/settings";
import { gameMenu, goTo, setVisible } from "./lib/screens";

enum Screen { Main, HowTo, Options }

const mainMenu = gameMenu(["m-start", "m-howto", "m-options", "m-quit"]);
const optionsMenu = gameMenu(["o-music", "o-sfx", "o-crt", "o-fullscreen", "o-back"]);
const music = new Music("music_title");
let screen = Screen.Main;
let leaving = false;
let t: f32 = 0;
const squadronTimer = new Timer(1.5);

Settings.apply();
Settings.restoreWindow();
const crt = addCrt();
music.play(0.9);
UI.setText("hiscore", scoreText(hiscore()));
mainMenu.render();
show(Screen.Main);

function show(s: Screen): void {
  screen = s;
  setVisible("main-menu", s == Screen.Main);
  setVisible("howto", s == Screen.HowTo);
  setVisible("options", s == Screen.Options);
  setVisible("prompt", s != Screen.HowTo);
}

function renderOptions(): void {
  UI.fill("o-music-fill", Settings.musicVolume);
  UI.fill("o-sfx-fill", Settings.sfxVolume);
  UI.setText("o-crt-value", Settings.crt ? "ON" : "OFF");
  UI.setText("o-fullscreen-value", Settings.fullscreen ? "ON" : "OFF");
  optionsMenu.render();
}

function updateMain(): void {
  const choice = mainMenu.update();
  if (choice == "m-start") {
    leaving = true;
    Session.newGame();
    music.fadeOut(0.8);
    goTo(stageScene(1));
  } else if (choice == "m-howto") {
    show(Screen.HowTo);
  } else if (choice == "m-options") {
    optionsMenu.select(0);
    renderOptions();
    show(Screen.Options);
  } else if (choice == "m-quit") {
    App.quit();
  }
}

// Left/right adjust the selected option; confirm toggles on/off ones.
function updateOptions(): void {
  const choice = optionsMenu.update();
  const step: f32 = Input.pressed("right") ? 1 : Input.pressed("left") ? -1 : 0;
  const item = optionsMenu.selected;
  if (step != 0 && item == "o-music") Settings.musicVolume += step * 0.1;
  if (step != 0 && item == "o-sfx") Settings.sfxVolume += step * 0.1;
  if (step != 0 && (item == "o-music" || item == "o-sfx")) sfx("menu_move", 0.8);
  if (item == "o-crt" && (step != 0 || choice == item)) {
    Settings.crt = !Settings.crt;
    crt.enabled = Settings.crt;
  }
  if (item == "o-fullscreen" && (step != 0 || choice == item)) Settings.fullscreen = !Settings.fullscreen;
  if (choice == "o-back" || Input.pressed("back")) {
    if (choice != "o-back") sfx("menu_back", 0.7);
    show(Screen.Main);
    return;
  }
  renderOptions();
}

function spawnSquadron(): void {
  const ship = Random.pick(["ship_0000", "ship_0004", "ship_0008", "ship_0001"]);
  const cx = Random.range(-150, 150);
  const planes = Random.int(3, 5);
  for (let i = 0; i < planes; i++) {
    const side: f32 = <f32>((i + 1) / 2) * (i % 2 == 0 ? 1 : -1);  // V formation
    spawn("attract_plane", cx + side * 44, -380 - Mathf.abs(side) * 40,
          new Overrides().texture("assets/atlases/shmup.atlas.json#" + ship));
  }
}

export function onUpdate(dt: f32): void {
  t += dt;
  handleGlobalKeys();
  if (squadronTimer.tick(dt)) {
    squadronTimer.start(Random.range(5, 9));
    spawnSquadron();
  }
  UI.opacity("prompt", blink(t, 2, 1, 0.4));
  if (leaving) return;

  if (screen == Screen.Main) {
    updateMain();
  } else if (screen == Screen.HowTo) {
    if (Input.pressed("confirm") || Input.pressed("back")) {
      sfx("menu_back", 0.7);
      show(Screen.Main);
    }
  } else {
    updateOptions();
  }
}
