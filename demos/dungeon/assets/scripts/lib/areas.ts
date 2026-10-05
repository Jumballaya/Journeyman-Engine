// The two areas. Each is a scene of the same name whose "map" entity draws an
// ASCII file (assets/maps/<id>.txt, top row first, one character per 16px tile)
// with dungeon.tileset.json. Areas are grids of 16x11-tile rooms; the camera
// shows one room at a time.
//   .  floor        :  dark floor    #  wall          T  tree / statue   R  rock
//   W  water        s  sand path     f  flowers       F  fire            H  hermit
//   +  locked door  S  stairs down   U  stairs up     P  arrival point
//   e  slime        b  bat           k  skeleton      O  Ogloth (boss)   X  where the shard appears
//   $  gem          y  key           h  heart container

export const ROOM_W = 16;
export const ROOM_H = 11;

export class Area {
  constructor(readonly id: string, readonly music: string, readonly outdoors: bool) {}
}



export const AREAS: Area[] = [
  new Area("grove", "music_grove", true),
  new Area("crypt", "music_crypt", false),
];

// The area with this id; the first one if unknown.
export function areaById(id: string): Area {
  for (let i = 0; i < AREAS.length; i++) {
    if (AREAS[i].id == id) return AREAS[i];
  }
  return AREAS[0];
}
