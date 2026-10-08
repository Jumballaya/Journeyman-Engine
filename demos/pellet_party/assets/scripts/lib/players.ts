// Each player's color, by player id (0 hosts).
const R: f32[] = [0.35, 1.0, 0.45, 1.0];
const G: f32[] = [0.65, 0.42, 0.9, 0.8];
const B: f32[] = [1.0, 0.4, 0.45, 0.3];
const CSS: string[] = ["#5aa6ff", "#ff6b66", "#73e673", "#ffcc4d"];
const NAMES: string[] = ["BLUE", "RED", "GREEN", "GOLD"];

function slot(player: i32): i32 { return ((player % 4) + 4) % 4; }

export function red(player: i32): f32 { return R[slot(player)]; }
export function green(player: i32): f32 { return G[slot(player)]; }
export function blue(player: i32): f32 { return B[slot(player)]; }
export function css(player: i32): string { return CSS[slot(player)]; }
export function colorName(player: i32): string { return NAMES[slot(player)]; }

// The round, in the session store (GameState): every machine reads it, the
// host writes it. Kept there (not in a script's variables) so a host that
// takes over when the old one leaves carries on from it.
export const PHASE = "phase";        // "start" (lobby asked for a round), "play", "results"
export const TIME_LEFT = "timeLeft";  // seconds
export const WINNER = "winner";      // player id
export function scoreKey(player: i32): string { return "score." + player.toString(); }
