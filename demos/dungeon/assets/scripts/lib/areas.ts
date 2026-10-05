// The two areas as ASCII maps, top row first; one character per 16px tile.
// Each area is a grid of 16x11-tile rooms; the camera shows one room at a time.
//   .  floor        :  dark floor    #  wall          T  tree / statue   R  rock
//   W  water        s  sand path     f  flowers       F  fire            H  hermit
//   +  locked door  S  stairs down   U  stairs up     P  arrival point
//   e  slime        b  bat           k  skeleton      O  Ogloth (boss)   X  where the shard appears
//   $  gem          y  key           h  heart container

export const ROOM_W = 16;
export const ROOM_H = 11;

export class Area {
  constructor(readonly id: string, readonly map: string[], readonly music: string, readonly outdoors: bool) {}
  get width(): i32 { return this.map[0].length; }
  get height(): i32 { return this.map.length; }
}

const GROVE: string[] = [
  "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",
  "T..............TT..............T",
  "T..............TT....RRRRRR....T",
  "T..R.R.R.......TT....R.S..R....T",
  "T..........b...TT....R....R....T",
  "T....................RR..RR....T",
  "T...b..........TT..............T",
  "T...........$..TT..k........k..T",
  "T.h............TT..............T",
  "T..............TT..............T",
  "TTTTTTT..TTTTTTTTTTTTTT..TTTTTTT",
  "TTTTTTT..TTTTTTTTTTTTTT..TTTTTTT",
  "T..............TT..............T",
  "T...........f..TT.R..........$.T",
  "T.FHF..........TT........e.....T",
  "T..............TT..e...........T",
  "T....ssssss.................e..T",
  "T..f...........TT..............T",
  "T......P.......TT...WW..WWWW.R.T",
  "T.f..........f.TT...WW..WWWW...T",
  "T..............TT..............T",
  "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",
];

const CRYPT: string[] = [
  "################################################",
  "#..............##..............##.:.:.:.:.:.:.:#",
  "#..............##..::::::::::..##:.:.:.:.:.:.:.#",
  "#...k..........##..............##.:.:.:.:.:.:.:#",
  "#..............##..............##:.:.:.:.:.:.:.#",
  "#......y.............b....b....+..:.:.:.:.O.X.:#",
  "#..............##..............##:.:.:.:.:.:.:.#",
  "#..e.......k...##......e.......##.:.:.:.:.:.:.:#",
  "#..............##..............##:.:.:.:.:.:.:.#",
  "#..............##..............##.:.:.:.:.:.:.:#",
  "#######################..#######################",
  "#######################++#######################",
  "#..............##..............##..............#",
  "#..............##..............##.::::::::::h:.#",
  "#..............##...T......T...##..............#",
  "#........e.....##..............##..............#",
  "#....................k....k..........b....b....#",
  "#..............##..............##..............#",
  "#..U.......e...##...T......T...##..............#",
  "#......P.......##.......y......##..$........$..#",
  "#..............##..............##..............#",
  "################################################",
];

export const AREAS: Area[] = [
  new Area("grove", GROVE, "music_grove", true),
  new Area("crypt", CRYPT, "music_crypt", false),
];

// The area with this id; the first one if unknown.
export function areaById(id: string): Area {
  for (let i = 0; i < AREAS.length; i++) {
    if (AREAS[i].id == id) return AREAS[i];
  }
  return AREAS[0];
}
