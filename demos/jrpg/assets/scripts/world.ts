// Runs the map the party is on (Party.map): loads its tiles into the "map"
// entity, spawns the people and things it marks, follows the hero with the
// camera, shows the place name, and opens the party menu (Esc). runWhenPaused.
import { Input, Music, Overrides, Renderer, TileMap, Time, UI, World, spawn } from "@jm/runtime";
import { ITEMS } from "./lib/data";
import { mapById } from "./lib/maps";
import { Party } from "./lib/party";
import { FIGURE_LIFT, TILE } from "./lib/tiles";
import { follow } from "./lib/view";

const ATLAS = "assets/atlases/sprites.atlas.json#";
const PLACE_SECONDS: f32 = 2.5;

// Who and what stands on the map, by map character.
const ELDER = "THE CINDER WYRM HAS WOKEN IN THE EMBERWOOD.|ITS FIRE WILL REACH ALDANE BY WINTER.|KAEL, LYRA, BRAM... PLEASE. STOP IT.";
const ELDER_AGAIN = "ITS LAIR LIES NORTH-EAST, PAST THE RIVER.|REST AT THE INN BEFORE YOU GO.";
const CHILD = "THE WYRM BREATHES FIRE, SO IT MUST HATE THE COLD!|KAEL AND LYRA CAN STRIKE TOGETHER. FLAME BLADE!";

if (!Party.started) Party.newGame();  // launched directly (JM_ENTRY_SCENE)
const gameMap = mapById(Party.map);
const map = TileMap.find("map");
const music = new Music(gameMap.music);
let placeShown: f32 = 0;
let menuOpen = false;

Renderer.setClearColor(0, 0, 0);
map.load(gameMap.tiles);
populate();
spawnHero();
music.play(0.55);
UI.setText("place", gameMap.name);

// Someone standing on the map, feet on the tile: `prefab` is their role
// (npc, innkeeper, smith); `look` names their sprites (elder, inn, smith, child).
function person(prefab: string, look: string, x: f32, y: f32, o: Overrides): void {
  spawn("shadow", x, y - 6);
  spawn(prefab, x, y + FIGURE_LIFT, o.texture(ATLAS + look + "_down_0"));
}

function populate(): void {
  let chests = 0;
  for (let ty = 0; ty < map.height; ty++) {
    for (let tx = 0; tx < map.width; tx++) {
      const c = map.at(tx, ty);
      const x = map.centerX(tx), y = map.centerY(ty);
      if (c == "c") {
        const flag = gameMap.id + ".chest." + (chests++).toString();
        spawn("chest", x, y, new Overrides().paramText("flag", flag).paramText("item", chestItem(flag)).param("count", chestItem(flag) == "POTION" ? 2 : 1));
      } else if (c == "S") spawn("crystal", x, y);
      else if (c == "L") spawn("lair", x, y);
      else if (c == "E") person("npc", "elder", x, y, new Overrides().paramText("lines", ELDER).paramText("again", ELDER_AGAIN));
      else if (c == "K") person("npc", "child", x, y, new Overrides().paramText("lines", CHILD));
      else if (c == "I") person("innkeeper", "inn", x, y, new Overrides().param("price", 10));
      else if (c == "M") person("smith", "smith", x, y, new Overrides());
    }
  }
}

// What each chest holds, by its flag (the map's chests in reading order).
function chestItem(flag: string): string {
  if (flag == "town.chest.0") return "ETHER";
  if (flag == "town.chest.1") return "POTION";
  if (flag == "field.chest.0") return "PHOENIX";
  if (flag == "field.chest.1") return "HI-POTION";
  return "ETHER";
}

// At the saved spot, beside the exit the party came through (facing away
// from it), or where a new game starts.
function spawnHero(): void {
  if (Party.x >= 0) {
    spawn("hero", Party.x, Party.y);
    return;
  }
  const exit = Party.arrivingBy;
  const at = map.positionsOf(exit.length > 0 ? exit : "P");
  if (at.length == 0) return;
  const step = exit == "<" ? 1 : exit == ">" ? -1 : 0;
  spawn("hero", map.centerX(at[0] + step), map.centerY(at[1]));
}

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
  if (Input.pressed("menu") && (menuOpen || !Time.paused)) showMenu(!menuOpen);  // not over a dialog
  placeShown += dt;
  UI.setVisible("place", placeShown < PLACE_SECONDS, "hidden");
  const hero = World.find("hero");
  if (!hero.isNone) follow(hero.transform.x, hero.transform.y, <f32>map.width * TILE, <f32>map.height * TILE);
}
