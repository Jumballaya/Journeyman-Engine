// The two overworld maps as ASCII, top row first; one character per 16px tile.
//   .  grass       "  tall grass (random battles)   ,  path     =  bridge
//   T  tree        r  rock        ~  water     R  roof     w  wall     n  window    d  door
//   F  fence       f  flowers     c  chest     S  save crystal         L  the wyrm's lair
//   E I M K  the elder, innkeeper, smith and a child
//   >  exit to the Emberwood      <  exit to Aldane          P  where a new game starts

export class GameMap {
  constructor(readonly id: string, readonly name: string, readonly rows: string[], readonly music: string,
              readonly encounters: bool) {}
  get width(): i32 { return this.rows[0].length; }
  get height(): i32 { return this.rows.length; }
}

const TOWN: string[] = [
  "TTTTTTTTTTTTTTTTTTTTTTTTTT",
  "T........RRRRRRRR........T",
  "T.~~~~~..RRRRRRRR........T",
  "T.~~~~~..wnwwnwww.FFFFFF.T",
  "T.~~~~~..wwwdwwww........T",
  "T.~~~~~.....,,..........fT",
  "T...........,E..f.....c..T",
  "T.c.......f.,,...........T",
  "T,,P,,,,,,,,,,,,,,,,,,,,,>",
  "T,,f,,,,K,,,,,,S,,,,,,,,,>",
  "T...........,,...........T",
  "T..RRRRRR...,,...RRRRRR..T",
  "T..RRRRRR...,,...RRRRRR..T",
  "T..wnwwnw...,,...wnwwnw..T",
  "T..wwdwww...,,...wwdwww..T",
  "T........................T",
  "T.....I.............M....T",
  "TTTTTTTTTTTTTTTTTTTTTTTTTT",
];

const FIELD: string[] = [
  "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",
  "T.................~~\"\"\"\"\"\"r\"\"\"rrrrrrr.TT",
  "T...........T.....~~\"\"\"\"\"\"\"\"\"Tr..L..r..T",
  "T..\"\"\"\"\"\"\"\"\"\"\"....~~\"\"\"\"\"c\"\"T\"\".,,.T...T",
  "T..\"\"\"\"\"\"\"rT\"T....~~\"\"\"\"\"\"\"T\"\"\".,,.....T",
  "T..\"\"r\"\"\"T\"T\"\"....~~.......T....,,.....T",
  "T..\"\"\"\"\"\"\"\"\"\"\"..,,==,,,,,,,,,,,,,,.r...T",
  "TT.\"\"\"\"\"\"\"\"\"\"\"..,,==,,,,,,,,,,,,,,.....T",
  "T..T\"\"\"\"\"\"\"\"\"\"..,,~~.................T.T",
  "T..\"\"\"\"\"\"\"\"\"\"\"..,,~~...........T..r....T",
  "T..\"\"T\"\"\"\"\"\"\"\"..,,~~\"T\"\"\"T\"\"\"\"\"\"\"\"\"\"\"..T",
  "T..\"\"T\"\"\"\"T\"\"\"..,,~~\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "T...............,,~~T\"\"\"\"\"\"\"T\"T\"\"\"\"\"\"..T",
  "T...............,S~~\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "<P,,,,,,,,,,,,,,,,==\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "<,,,,,,,,,,,,,,,,,==\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "T.................~~\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "T.................~~\"\"T\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "T..\"\"\"\"\"\"\"\"\"\"T\"...~~\"\"\"\"\"T\"\"\"\"\"\"\"\"\"\"\"..T",
  "T..\"\"\"\"T\"\"\"\"\"\"\"...~~\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "T..\"\"\"r\"\"\"\"\"\"\"\"...~~\"\"\"\"\"\"\"\"\"\"T\"\"\"\"\"\"..T",
  "T..\"\"\"T\"\"\"\"\"\"\"\".T.~~\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "T..\"\"\"\"\"\"\"\"\"\"\"\"...~~..................TT",
  "T..\"\"\"\"\"\"\"\"\"T\"\"...~~.\"\"\"\"\"\"\"\"\"\"\"T\"\"\"\"..T",
  "T.T\"\"\"\"\"cT\"T\"\"\"..T~~.\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "T..\"\"\"\"\"\"\"\"\"\"\"\"...~~.\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"..T",
  "T..r\"T\"\"\"\"\"\"\"\"\"...~~.\"\"\"\"\"\"\"\"\"\"\"\"\"\"c\"..T",
  "T.................~~.\"T\"\"\"\"\"\"\"\"\"\"\"\"\"\".TT",
  "T...r......T......~~...........T....T..T",
  "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT",
];

export const MAPS: GameMap[] = [
  new GameMap("town", "ALDANE", TOWN, "music_town", false),
  new GameMap("field", "EMBERWOOD", FIELD, "music_field", true),
];

export function mapById(id: string): GameMap {
  for (let i = 0; i < MAPS.length; i++) if (MAPS[i].id == id) return MAPS[i];
  return MAPS[0];
}
