// Integer display for scores/counters: floor, clamp to zero, pad to a minimum
// width. Never truncates larger numbers. Width is chosen by the game.
export function formatNumber(value: f64, digits: i32 = 1): string {
  const text = (<i64>Math.max(0, Math.floor(value))).toString();
  return text.length >= digits ? text : "0".repeat(digits - text.length) + text;
}

export function formatPercent(fraction: f64): string {
  return (<i32>Math.round(Math.max(0, Math.min(1, fraction)) * 100)).toString() + "%";
}
