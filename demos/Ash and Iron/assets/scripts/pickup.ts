// Something lying on the ground, taken by walking onto it. "item": its id in
// the items table; "count"; "key": a unique name, so it stays taken.
import { Params } from "@jm/runtime";

Params.text("item", "tonic");
Params.number("count", 1);
Params.text("key", "");
