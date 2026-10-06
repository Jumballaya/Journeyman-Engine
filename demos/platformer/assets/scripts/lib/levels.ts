// The three levels. Each is a scene, scenes/level_<id>.scene.json, authoring
// everything in it: blocks, coins, enemies, Pip, decorations, the flagpole.
// Its tiles are a Tiled map (assets/maps/<id>.tmj, 16px tiles from pip.tsj,
// one look per theme). Blocks put their solid, bumpable tile on the map's
// hidden "collision" layer.

export enum Theme { Overworld, Underground, Castle }

export class Level {
  constructor(readonly id: string, readonly theme: Theme, readonly seconds: i32) {}
  get last(): bool { return this.id == LEVELS[LEVELS.length - 1].id; }
}

export const LEVELS: Level[] = [
  new Level("1-1", Theme.Overworld, 300),
  new Level("1-2", Theme.Underground, 300),
  new Level("1-3", Theme.Castle, 300),
];

export function levelScene(id: string): string { return "level_" + id; }

// The level with this id; the first one if unknown.
export function levelById(id: string): Level {
  for (let i = 0; i < LEVELS.length; i++) {
    if (LEVELS[i].id == id) return LEVELS[i];
  }
  return LEVELS[0];
}

export function nextLevelId(id: string): string {
  for (let i = 0; i + 1 < LEVELS.length; i++) {
    if (LEVELS[i].id == id) return LEVELS[i + 1].id;
  }
  return "";
}
