import { __jmMessageFrom, __jmMessageName, __jmMessageText, __jmMessageNumber, __jmMessagePlayer, __jmDataRead } from "./env";
import { Entity } from "./entity";
import { Json, JsonValue } from "./json";
import { utf8, buf, cap, grow, text } from "./util";

// What entity.send delivered: a script receives it by exporting
//   export function onMessage(message: Message): void { ... }
export class Message {
  // `player`: in a multiplayer session, the player whose machine sent it
  // (Net.HOST from a dedicated server); -2 offline.
  constructor(readonly from: Entity, readonly name: string, readonly text: string, readonly number: f64,
              readonly player: i32 = -2) {}

  // The message being delivered. Internal: the generated entry calls it.
  static current(): Message {
    let n = __jmMessageName(buf(), cap());
    if (grow(n)) n = __jmMessageName(buf(), cap());
    const name = text(n, "");
    n = __jmMessageText(buf(), cap());
    if (grow(n)) n = __jmMessageText(buf(), cap());
    return new Message(Entity.unpack(__jmMessageFrom()), name, text(n, ""), __jmMessageNumber(), __jmMessagePlayer());
  }
}

// Text and JSON files listed in the manifest, so content can live in data:
// Data.json("enemies") reads assets/.../enemies.json.
export class Data {
  // The file's text, or "" if it can't be read (logged).
  static text(path: string): string {
    const p = utf8(path);
    let n = __jmDataRead(p.dataStart, p.length, buf(), cap());
    if (grow(n)) n = __jmDataRead(p.dataStart, p.length, buf(), cap());
    return text(n, "");
  }

  // The parsed file; a null value if it can't be read or parsed.
  static json(path: string): JsonValue { return Json.parse(Data.text(path)); }
}
