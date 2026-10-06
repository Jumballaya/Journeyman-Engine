// The two areas. Each is a scene of the same name whose "map" entity draws a
// Tiled map (assets/maps/<id>.tmj, 16px tiles from dungeon.tsj). Areas are grids
// of 16x11-tile rooms; the camera shows one room at a time. Tile types scripts
// use: "door" (locked, on the hidden collision layer), "stairs_down", "stairs_up".
// Everything else is authored in the scene. A room's enemies and items are in
// its group, "room-<x>-<y>" (see roomGroup), spawned while the hero is in it.

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
