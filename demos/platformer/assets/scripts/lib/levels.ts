// The three levels. Each one's tiles are an ASCII file (assets/maps/<id>.txt,
// top row first, one character per 16px tile) drawn by pip.tileset.json:
//   #  ground        B  brick        ?  coin block    M  mushroom block   X  hard block
//   [] pipe top      {} pipe body    o  coin          g  gloop            k  beetle
//   |  flagpole      C  castle (4 tiles right of its flagpole)   ~  lava   =  bridge   *  gem (goal)
//   K  Gloop King    P  player start c  cloud         h  hill             b  bush

export enum Theme { Overworld, Underground, Castle }

export class Level {
  constructor(readonly id: string, readonly theme: Theme, readonly seconds: i32) {}
  get tiles(): string { return "assets/maps/" + this.id + ".txt"; }
  get last(): bool { return this.id == LEVELS[LEVELS.length - 1].id; }
}

export const LEVELS: Level[] = [
  new Level("1-1", Theme.Overworld, 300),
  new Level("1-2", Theme.Underground, 300),
  new Level("1-3", Theme.Castle, 300),
];

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
