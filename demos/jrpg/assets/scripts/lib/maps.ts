// The two overworld maps. Their tiles are ASCII files (assets/maps/*.txt), top
// row first, one character per 16px tile, drawn by aldane.tileset.json:
//   .  grass       "  tall grass (random battles)   ,  path     =  bridge
//   T  tree        r  rock        ~  water     R  roof     w  wall     n  window    d  door
//   F  fence       f  flowers     c  chest     S  save crystal         L  the wyrm's lair
//   E I M K  the elder, innkeeper, smith and a child
//   >  exit to the Emberwood      <  exit to Aldane          P  where a new game starts

export class GameMap {
  constructor(readonly id: string, readonly name: string, readonly music: string, readonly encounters: bool) {}
  get tiles(): string { return "assets/maps/" + this.id + ".txt"; }
}

export const MAPS: GameMap[] = [
  new GameMap("town", "ALDANE", "music_town", false),
  new GameMap("field", "EMBERWOOD", "music_field", true),
];

export function mapById(id: string): GameMap {
  for (let i = 0; i < MAPS.length; i++) if (MAPS[i].id == id) return MAPS[i];
  return MAPS[0];
}
