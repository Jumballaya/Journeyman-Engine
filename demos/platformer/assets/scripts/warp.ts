// A test scene's whole job: a new game, then straight into a level. Params:
// "level" ("1-3") and "startX" (Pip's start column).
import { Params, Scene } from "@jm/runtime";
import { levelScene } from "./lib/levels";
import { Session } from "./lib/session";

Session.newGame();
Session.level = Params.text("level", Session.level);
if (Params.number("startX", -1) >= 0) Session.startX = <i32>Params.number("startX");
Scene.load(levelScene(Session.level));
