import { Launch, podAim } from "../assets/scripts/lib/pods";

export function fixedAimIgnoresTimeAndRotationSweepsSlowly(): void {
  assert(podAim(44, false, 8) == 44);
  assert(podAim(30, true, 0) == 30);
  assert(Mathf.abs(podAim(30, true, Mathf.PI) - 36) < 0.001);
  assert(Mathf.abs(podAim(30, true, 3 * Mathf.PI) - 24) < 0.001);
  assert(Mathf.abs(podAim(30, true, 0.1) - 30) < 0.31);
}

export function launchFollowsAimAtTheAuthoredSpeed(): void {
  const right = new Launch(0, 1250), up = new Launch(90, 800);
  assert(right.x == 1250 && right.y == 0);
  assert(Mathf.abs(up.x) < 0.001 && up.y == 800);
  const diagonal = new Launch(44, 1250);
  assert(diagonal.x > 0 && diagonal.y > 0);
  assert(Mathf.abs(Mathf.sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y) - 1250) < 0.01);
  const stopped = new Launch(20, -5);
  assert(stopped.x == 0 && stopped.y == 0);
}
