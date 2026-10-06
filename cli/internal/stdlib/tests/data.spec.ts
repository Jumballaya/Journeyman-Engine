import { Json, JsonValue } from "../runtime/json";
import { GameState } from "../runtime/state";
import { Overrides } from "../runtime/world";
import { Entity } from "../runtime/entity";
import { Message, Data } from "../runtime/message";

export function jsonParses(): void {
  const v = Json.parse(' {"name": "Kael", "hp": 62, "tags": ["hero", "fire"], "boss": false, "x": -1.5e1,'
                       + ' "esc": "a\\"b\\n\\u0041", "none": null, "nested": {"list": [1, [2]]}} ');
  assert(v.get("name").text() == "Kael");
  assert(v.get("hp").int() == 62);
  assert(v.get("tags").length == 2 && v.get("tags").at(1).text() == "fire");
  assert(!v.get("boss").bool(true));
  assert(v.get("x").number() == -15);
  assert(v.get("esc").text() == "a\"b\nA");
  assert(v.get("none").isNull && v.has("none"));
  assert(v.get("nested").get("list").at(1).at(0).int() == 2);
  assert(v.get("missing").get("deeper").number(7) == 7);  // missing paths fall back
  assert(v.keys().join(",") == "name,hp,tags,boss,x,esc,none,nested");
}

export function jsonRejectsMalformed(): void {
  assert(Json.parse("{").isNull);
  assert(Json.parse("[1,]").isNull);
  assert(Json.parse("12 13").isNull);
  assert(Json.parse("").isNull);
}

export function jsonRoundTrips(): string {
  const v = new JsonValue().set("a", JsonValue.number(3)).set("b", JsonValue.strings(["x", "y\"z"]))
    .set("c", JsonValue.bool(true)).set("d", JsonValue.number(0.25));
  const again = Json.parse(v.toString());
  assert(again.toString() == v.toString());
  return v.toString();
}

export function storeLists(): void {
  GameState.setStrings("party", ["kael", "lyra"]);
  GameState.setNumbers("hp", [62, 40]);
  assert(GameState.getStrings("party").join(",") == "kael,lyra");
  assert(GameState.getNumbers("hp")[1] == 40);
  assert(GameState.getJson("missing").isNull);
  assert(GameState.keys("pa").join(",") == "party");
}

export function overrideTags(): string {
  return new Overrides().tag("door").tag("locked").toJson();
}

export function messageAndData(): void {
  const m = Message.current();
  assert(m.name == "talk" && m.text == "hello" && m.number == 2 && m.from.index == 7);
  assert(Data.json("enemies").at(0).get("name").text() == "JELLY");
  assert(Data.text("missing.txt") == "");
}

export function entityMailbox(): void {
  new Entity(3, 1).send("hit", "", 4);
}
