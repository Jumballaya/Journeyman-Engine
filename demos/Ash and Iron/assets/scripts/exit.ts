// A way to another scene, taken by stepping onto it (or bumping its closed
// gate). "to": the scene; "at": the spot there to arrive at (an entity's
// name); "when": a condition (has:foundry_key) that must hold; "locked":
// what the hero hears when it doesn't.
import { Params } from "@jm/runtime";

Params.text("to", "");
Params.text("at", "");
Params.text("when", "");
Params.text("locked", "It won't open.");
