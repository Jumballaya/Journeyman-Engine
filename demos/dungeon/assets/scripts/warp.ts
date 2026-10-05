// A test scene's whole job: a new game set up by params, then straight into an
// area. Params: "area" (scene), start tile "tx"/"ty", "sword", "keys", "hearts".
import { Params, Scene } from "@jm/runtime";
import { Session } from "./lib/session";

Session.newGame();
Session.area = Params.text("area", "grove");
if (Params.number("sword") > 0) Session.hasSword = true;
if (Params.number("keys") > 0) Session.keys = <i32>Params.number("keys");
for (let i = Session.maxHearts; i < <i32>Params.number("hearts"); i++) Session.addHeartContainer();
if (Params.number("tx", -1) >= 0) Session.setWarp(<i32>Params.number("tx"), <i32>Params.number("ty"));
Scene.load(Session.area);
