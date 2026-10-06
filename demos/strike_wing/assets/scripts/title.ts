// Title screen: main menu, how-to-play, options (volumes, CRT, fullscreen)
// and an attract-mode squadron flying past.
import { settings, crt, screen } from "./lib/presentation";
import { Audio, Timer, blink, Random, App, Input, Music, Overrides, UI, spawn } from "@jm/runtime";
import { scoreText } from "./lib/util";
import * as Session from "./lib/session";
import { hiscore, stageScene } from "./lib/session";

const panels = screen.panels(["main-menu", "howto", "options"]);

const mainMenu = screen.menu(["m-start", "m-howto", "m-options", "m-quit"]);
const optionsMenu = screen.menu(["o-music", "o-sfx", "o-crt", "o-fullscreen", "o-back"]);
const music = new Music("music_title");
let leaving = false;
let t: f32 = 0;
const squadronTimer = new Timer(1.5);

screen.open();
music.play(0.9);
UI.setText("hiscore", scoreText(hiscore()));
mainMenu.render();
show("main-menu");

function show(panel: string): void {
  panels.show(panel);
  screen.setVisible("prompt", panel != "howto");
}

function renderOptions(): void {
  UI.fill("o-music-fill", settings.musicVolume);
  UI.fill("o-sfx-fill", settings.sfxVolume);
  UI.setText("o-crt-value", crt.value ? "ON" : "OFF");
  UI.setText("o-fullscreen-value", settings.fullscreen ? "ON" : "OFF");
  optionsMenu.render();
}

function updateMain(): void {
  const choice = mainMenu.update();
  if (choice == "m-start") {
    leaving = true;
    Session.newGame();
    music.fadeOut(0.8);
    screen.goTo(stageScene(1));
  } else if (choice == "m-howto") {
    show("howto");
  } else if (choice == "m-options") {
    optionsMenu.select(0);
    renderOptions();
    show("options");
  } else if (choice == "m-quit") {
    App.quit();
  }
}

// Left/right adjust the selected option; confirm toggles on/off ones.
function updateOptions(): void {
  const choice = optionsMenu.update();
  const step: f32 = Input.pressed("right") ? 1 : Input.pressed("left") ? -1 : 0;
  const item = optionsMenu.selected;
  if (step != 0 && item == "o-music") settings.musicVolume += step * 0.1;
  if (step != 0 && item == "o-sfx") settings.sfxVolume += step * 0.1;
  if (step != 0 && (item == "o-music" || item == "o-sfx")) Audio.play("menu_move", 0.8);
  if (item == "o-crt" && (step != 0 || choice == item)) {
    crt.value = !crt.value;
  }
  if (item == "o-fullscreen" && (step != 0 || choice == item)) settings.fullscreen = !settings.fullscreen;
  if (choice == "o-back" || Input.pressed("back")) {
    if (choice != "o-back") Audio.play("menu_back", 0.7);
    show("main-menu");
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
  screen.update();
  if (squadronTimer.tick(dt)) {
    squadronTimer.start(Random.range(5, 9));
    spawnSquadron();
  }
  UI.opacity("prompt", blink(t, 2, 1, 0.4));
  if (leaving) return;

  if (panels.active == "main-menu") {
    updateMain();
  } else if (panels.active == "howto") {
    if (Input.pressed("confirm") || Input.pressed("back")) {
      Audio.play("menu_back", 0.7);
      show("main-menu");
    }
  } else {
    updateOptions();
  }
}
