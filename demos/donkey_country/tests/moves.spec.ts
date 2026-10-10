import { ACCEL, BUFFER, COYOTE, RUN, jumps, pose, run } from "../assets/scripts/lib/moves";

export function runsUpToTopSpeedAndStops(): void {
  let vx: f32 = 0;
  for (let i = 0; i < 60; i++) vx = run(vx, 1, true, 1.0 / 60);
  assert(vx == RUN, "reaches top speed in a second");
  vx = run(vx, 0, true, 1.0 / 60);
  assert(vx == RUN - ACCEL / 60, "slows down when let go");
  assert(run(0, -1, false, 0.1) < 0, "steers in the air too");
}

export function jumpsForgiveLateAndEarlyPresses(): void {
  assert(jumps(0, 0), "pressed while standing");
  assert(jumps(0, COYOTE), "just off a ledge still counts");
  assert(!jumps(0, COYOTE + 0.01), "falling for a while doesn't");
  assert(jumps(BUFFER, 0), "pressed just before landing");
  assert(!jumps(BUFFER + 0.01, 0), "pressed long before landing doesn't");
}

export function posesFollowTheMotion(): void {
  assert(pose(0, 0, true) == "idle");
  assert(pose(200, 0, true) == "run");
  assert(pose(0, 300, false) == "jump");
  assert(pose(0, -300, false) == "fall");
}
