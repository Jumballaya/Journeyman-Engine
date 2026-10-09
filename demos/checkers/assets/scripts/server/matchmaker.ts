// The matchmaker, on the dedicated server only (.jm.json net.server.scripts).
// Players who join wait in line; each two are introduced (the first will
// host) and then leave to play each other directly.
import { Net, log } from "@jm/runtime";

const waiting = new Array<i32>();

export function onUpdate(dt: f32): void {
  const joined = Net.joined();
  for (let i = 0; i < joined.length; i++) waiting.push(joined[i]);
  const left = Net.left();
  for (let i = 0; i < left.length; i++) {
    const at = waiting.indexOf(left[i]);
    if (at >= 0) waiting.splice(at, 1);
  }

  while (waiting.length >= 2) {
    const a = waiting.shift();
    const b = waiting.shift();
    // Addresses as this server sees them: what each one's router shows the internet.
    Net.send(a, "match", "host|" + Net.playerAddress(b) + "|" + Net.playerName(b));
    Net.send(b, "match", "join|" + Net.playerAddress(a) + "|" + Net.playerName(a));
    log("matched " + Net.playerName(a) + " (" + Net.playerAddress(a) + ") with " + Net.playerName(b) + " (" +
        Net.playerAddress(b) + ")");
  }
}
