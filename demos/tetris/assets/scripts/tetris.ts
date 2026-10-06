// A game of Tetris: input → Game actions, Game events → sounds and toasts,
// Game state → the well sprites and side panel. Also owns pause and game over.
import {
  GameState, Input, Menu, Music, Params, Save, Scene, Sound, Timer, UI, Window, blink, fadeOut, formatNumber,
} from "@jm/runtime";
import { Event, Game, Phase } from "./lib/game";
import { kindsFrom } from "./lib/pieces";
import { Well } from "./lib/well";
import { Panel } from "./lib/panel";

enum Mode { Playing, Paused, Over }

const music = new Music("music");
const sfx = new Map<u32, Sound>();
sfx.set(Event.Moved, new Sound("move"));
sfx.set(Event.Rotated, new Sound("rotate"));
sfx.set(Event.Held, new Sound("hold"));
sfx.set(Event.Locked, new Sound("lock"));
sfx.set(Event.HardDropped, new Sound("hard_drop"));
sfx.set(Event.Cleared, new Sound("clear"));
sfx.set(Event.Tetris, new Sound("tetris"));
sfx.set(Event.LevelUp, new Sound("level_up"));
sfx.set(Event.GameOver, new Sound("game_over"));

const SHIFT_DELAY: f32 = 0.16;  // seconds held before a direction repeats
const SHIFT_RATE: f32 = 0.05;   // seconds between repeats

const pauseMenu = menu(["p-resume", "p-restart", "p-title"]);
const overMenu = menu(["o-retry", "o-title"]);
const well = new Well();
const panel = new Panel();

// Params "rows" ("XXXXXXXXX.|..." bottom first) and "pieces" ("ITO...") set up puzzles.
const game = new Game({
  level: <i32>GameState.getNumber("startLevel", 1),
  rows: Params.text("rows").length > 0 ? Params.text("rows").split("|") : [],
  pieces: kindsFrom(Params.text("pieces")),
});
const overDelay = new Timer();  // ignore the drop key that topped out
let mode = Mode.Playing;
let t: f32 = 0;
let toastAge: f32 = 99;
let leaving = false;

music.play(0.6);

function menu(items: string[]): Menu {
  return new Menu(items).sounds(new Sound("menu_move"), new Sound("menu_select"), 0.6, 0.8);
}

function toast(text: string): void {
  UI.setText("toast", text);
  toastAge = 0;
}

function setMode(next: Mode): void {
  mode = next;
  UI.setVisible("pause", next == Mode.Paused, "hidden");
  UI.setVisible("over", next == Mode.Over, "hidden");
  music.gain = next == Mode.Playing ? 0.6 : 0.2;
}

function goTo(scene: string): void {
  leaving = true;
  music.fadeOut(0.5);
  Scene.transition(scene, 0.6);
}

function play(dt: f32): void {
  if (Input.pressed("pause") || !Window.focused) {
    pauseMenu.select(0);
    setMode(Mode.Paused);
    return;
  }
  // Delayed auto-shift: a step on press, then repeats while held.
  if (Input.repeated("left", SHIFT_DELAY, SHIFT_RATE)) game.shift(-1);
  if (Input.repeated("right", SHIFT_DELAY, SHIFT_RATE)) game.shift(1);
  if (Input.pressed("rotate_cw")) game.rotate(1);
  if (Input.pressed("rotate_ccw")) game.rotate(-1);
  if (Input.pressed("hold")) game.hold();
  game.setSoftDrop(Input.down("down"));
  if (Input.pressed("hard_drop")) game.hardDrop();
  game.update(dt);
  react(game.takeEvents());
}

// Plays each event's sound; only the most important of a drop/lock/clear chain.
function react(events: u32): void {
  if (events & Event.GameOver) {
    const record = Save.record("hiscore", game.score);
    UI.setVisible("record", record, "hidden");
    UI.setText("final-score", formatNumber(game.score, 1));
    UI.setText("final-lines", game.lines.toString());
    overMenu.select(0);
    overDelay.start(0.8);
    setMode(Mode.Over);
    music.fadeOut(0.3);
  }
  if (events & Event.LevelUp) toast("LEVEL " + game.level.toString());
  else if (events & Event.Tetris) toast("TETRIS!");

  const order: u32[] = [Event.GameOver, Event.LevelUp, Event.Tetris, Event.Cleared, Event.HardDropped,
                        Event.Locked, Event.Held, Event.Rotated, Event.Moved];
  for (let i = 0; i < order.length; i++) {
    if (events & order[i]) {
      sfx.get(order[i]).play(order[i] == Event.Moved ? 0.5 : 0.8);
      return;
    }
  }
}

export function onUpdate(dt: f32): void {
  t += dt;
  toastAge += dt;
  if (Input.pressed("fullscreen")) Window.fullscreen = !Window.fullscreen;

  if (!leaving) {
    if (mode == Mode.Playing) {
      play(dt);
    } else if (mode == Mode.Paused) {
      const choice = Input.pressed("pause") ? "p-resume" : pauseMenu.update();
      if (choice == "p-resume") setMode(Mode.Playing);
      else if (choice == "p-restart") goTo("game");
      else if (choice == "p-title") goTo("title");
    } else if (overDelay.tick(dt) || overDelay.ready) {
      const choice = overMenu.update();
      if (choice == "o-retry") goTo("game");
      else if (choice == "o-title") goTo("title");
    }
  }

  well.draw(game, game.phase == Phase.Clearing ? blink(t, 16) : 0);
  panel.draw(game, Save.getNumber("hiscore"));
  UI.opacity("toast", fadeOut(toastAge, 0.8, 0.4));
}
