// Runs an area scene (param "map": its id, which is also the scene's name):
// follows the hero with the camera, shows the place name, and opens the party
// menu (Esc). Who and what stands where is authored in the scene (people bring
// their shadows as prefab children). runWhenPaused.
import { Input, Music, Params, Renderer, TileMap, Time, UI, World } from "@jm/runtime";
import { ITEMS } from "./lib/data";
import { mapById } from "./lib/maps";
import { Party } from "./lib/party";
import { TILE } from "./lib/tiles";
import { follow } from "./lib/view";

const PLACE_SECONDS: f32 = 2.5;

if (!Party.started) Party.newGame();  // launched directly (JM_ENTRY_SCENE)
const gameMap = mapById(Params.text("map", Party.map));
if (Party.map != gameMap.id) Party.arriveAt(gameMap.id, "");  // opened directly, not walked into
const map = TileMap.find("map");
const music = new Music(gameMap.music);
let placeShown: f32 = 0;
let menuOpen = false;

Renderer.setClearColor(0, 0, 0);
music.play(0.55);
UI.setText("place", gameMap.name);

function showMenu(open: bool): void {
  menuOpen = open;
  UI.setVisible("menu", open, "hidden");
  if (open) Time.pause();
  else Time.resume();
  if (!open) return;
  const fighters = Party.fighters();
  for (let i = 0; i < fighters.length; i++) {
    const f = fighters[i];
    UI.setText("m-" + f.id, f.name + "  LV " + Party.level(f.id).toString() + "   HP " + f.hp.toString() + "/" +
      f.maxHp.toString() + "   MP " + f.mp.toString() + "/" + f.maxMp.toString() + (f.poisoned ? "  PSN" : ""));
  }
  let items = "";
  for (let i = 0; i < ITEMS.length; i++) {
    const n = Party.inventory.count(ITEMS[i].name);
    if (n > 0) items += ITEMS[i].name + " x" + n.toString() + "   ";
  }
  UI.setText("m-items", items.length > 0 ? items : "NONE");
  UI.setText("m-gold", Party.gold.toString());
}

export function onUpdate(dt: f32): void {
  if (Input.justPressed("menu") && (menuOpen || !Time.paused)) showMenu(!menuOpen);  // not over a dialog
  placeShown += dt;
  UI.setVisible("place", placeShown < PLACE_SECONDS, "hidden");
  const hero = World.find("hero");
  if (!hero.isNone) follow(hero.transform.x, hero.transform.y, <f32>map.width * TILE, <f32>map.height * TILE);
}
