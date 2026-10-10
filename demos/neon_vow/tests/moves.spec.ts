import { ACCEL, BUFFER, COYOTE, RUN, jumps, pose, run, stomps } from "../assets/scripts/lib/moves";

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

export function stompsAreJudgedOnTheFallBeforeContact(): void {
  assert(stomps(true, 30, 28), "falling onto its top");
  assert(!stomps(true, 10, 28), "falling, but beside it (feet below its top)");
  assert(!stomps(false, 30, 28), "rising into it from below or beside");
  // Two at once: the first stomp bounces, but both are judged on the fall before contact.
  const before = true, feet: f32 = 31;
  assert(stomps(before, feet, 28) && stomps(before, feet, 29), "both squashed, no hurt");
}
