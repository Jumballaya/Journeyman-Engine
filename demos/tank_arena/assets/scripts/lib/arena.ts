// What every machine agrees on: player colors, and the match in the session
// store (GameState), which the server writes and mirrors to every client.
const R: f32[] = [0.36, 1.0, 0.42, 1.0, 0.75, 0.3, 1.0, 0.85];
const G: f32[] = [0.62, 0.38, 0.88, 0.82, 0.45, 0.9, 0.6, 0.85];
const B: f32[] = [1.0, 0.36, 0.42, 0.28, 1.0, 0.9, 0.85, 0.85];
const CSS: string[] = ["#5c9eff", "#ff615c", "#6be06b", "#ffd147", "#bf73ff", "#4de6e6", "#ff99d9", "#d9d9d9"];
const NAMES: string[] = ["BLUE", "RED", "GREEN", "GOLD", "VIOLET", "CYAN", "PINK", "SILVER"];

function slot(player: i32): i32 { return ((player % 8) + 8) % 8; }

export function red(player: i32): f32 { return player < 0 ? 0.55 : R[slot(player)]; }
export function green(player: i32): f32 { return player < 0 ? 0.55 : G[slot(player)]; }
export function blue(player: i32): f32 { return player < 0 ? 0.58 : B[slot(player)]; }
export function css(player: i32): string { return player < 0 ? "#8c8c94" : CSS[slot(player)]; }
export function tankName(player: i32): string { return player < 0 ? "BOT" : NAMES[slot(player)]; }

export function scoreKey(player: i32): string { return "score." + player.toString(); }
export const PHASE = "phase";    // "play", or "over" while the winner shows
export const WINNER = "winner";  // player id
export const FEED = "feed";      // the latest event ("RED WRECKED BLUE"), with a count so the same text shows again
export const FEED_COUNT = "feedCount";
export const KILLS_TO_WIN = 10;
