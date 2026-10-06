// The two overworld maps: Tiled maps (assets/maps/<id>.tmj) of 16px tiles from
// aldane.tsj. Tall grass ("tall_grass") brings random battles; "exit" objects
// lead to another map (properties "to", the scene, and "arrive", its exit).
// Who and what stands on each map is authored in its scene (scenes/<id>.scene.json).

export class GameMap {
  constructor(readonly id: string, readonly name: string, readonly music: string, readonly encounters: bool) {}
}

export const MAPS: GameMap[] = [
  new GameMap("town", "ALDANE", "music_town", false),
  new GameMap("field", "EMBERWOOD", "music_field", true),
];

export function mapById(id: string): GameMap {
  for (let i = 0; i < MAPS.length; i++) if (MAPS[i].id == id) return MAPS[i];
  return MAPS[0];
}
