// The match, as everyone sees it. A matchmaker pairs two players; the first
// hosts a p2p session for the two of them and plays red.
// Kept in this machine's session store: "local." keys aren't mirrored from the host.
export const OPPONENT = "local.opponent";  // the other player's name, from the matchmaker
export const NOTE = "local.note";          // why we're back at the title, if something went wrong

// "host|<address>|<name>" or "join|<address>|<name>": the matchmaker's word to each player.
export class Pairing {
  constructor(readonly hosting: bool, readonly address: string, readonly opponent: string) {}

  static parse(text: string): Pairing | null {
    const parts = text.split("|");
    if (parts.length < 3 || (parts[0] != "host" && parts[0] != "join")) return null;
    return new Pairing(parts[0] == "host", parts[1], parts.slice(2).join("|"));
  }
}
