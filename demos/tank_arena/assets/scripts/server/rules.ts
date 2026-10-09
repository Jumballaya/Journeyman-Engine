// The match, run by the dedicated server only (.jm.json net.server.scripts):
// first to KILLS_TO_WIN wins, then a new match; a bot keeps a lone player company.
import { GameState, Net, Overrides, Random, World, spawn } from "@jm/runtime";
import { FEED, FEED_COUNT, KILLS_TO_WIN, PHASE, WINNER, scoreKey, tankName } from "../lib/arena";

const OVER_SECONDS: f64 = 5;

GameState.setString(PHASE, "play");

function announce(text: string): void {
  GameState.setString(FEED, text);
  GameState.add(FEED_COUNT, 1);
}

function keepBotCompany(): void {
  const bots = World.findAll("bot");
  const alone = Net.players().length == 1;
  if (alone && bots.length == 0) {
    const points = World.findAll("spawn");
    if (points.length == 0) return;
    const p = points[Random.int(0, points.length - 1)];
    spawn("tank", p.transform.x, p.transform.y, new Overrides().param("bot", 1).tag("bot"));
  } else if (!alone) {
    for (let i = 0; i < bots.length; i++) bots[i].destroy();
  }
}

export function onUpdate(dt: f32): void {
  const joined = Net.joined();
  for (let i = 0; i < joined.length; i++) announce(tankName(joined[i]) + " JOINED");
  const left = Net.left();
  for (let i = 0; i < left.length; i++) {
    GameState.remove(scoreKey(left[i]));
    announce(tankName(left[i]) + " LEFT");
  }
  keepBotCompany();

  if (GameState.getString(PHASE) == "over") {
    if (GameState.add("overIn", -dt) > 0) return;
    const players = Net.players();
    for (let i = 0; i < players.length; i++) GameState.setNumber(scoreKey(players[i]), 0);
    GameState.setNumber(scoreKey(-1), 0);
    GameState.setString(PHASE, "play");
    announce("NEW MATCH");
    return;
  }
  const players = Net.players();
  for (let i = 0; i < players.length; i++) {
    if (GameState.getNumber(scoreKey(players[i])) >= KILLS_TO_WIN) {
      GameState.setNumber(WINNER, players[i]);
      GameState.setString(PHASE, "over");
      GameState.setNumber("overIn", OVER_SECONDS);
      return;
    }
  }
}
