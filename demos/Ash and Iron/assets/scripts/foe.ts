// Someone who fights. "who": their id in the enemies table; "key": a name
// unique in the game, so a kill stays a kill (saved as gone.<key>).
import { Params } from "@jm/runtime";

Params.text("who", "raider");
Params.text("key", "");
