import { Phase, Run } from "../assets/scripts/lib/run";

export function thirdHitEndsTheRunAndRestartRestoresIt(): void {
  const run = new Run();
  run.collect();
  run.tick(12);
  assert(run.hurt() && run.lives == 2 && run.phase == Phase.Playing);
  assert(run.hurt() && run.lives == 1);
  assert(run.hurt() && run.lives == 0 && run.phase == Phase.GameOver);
  assert(!run.hurt() && !run.collect());
  run.clear();
  run.tick(5);
  assert(run.phase == Phase.GameOver && run.lives == 0 && run.seconds == 12);
  run.restart();
  assert(run.phase == Phase.Playing && run.lives == 3 && run.shards == 0 && run.seconds == 0);
}

export function clearFreezesTheScoreAndTimeUntilRestart(): void {
  const run = new Run();
  run.collect();
  run.collect();
  run.tick(23.5);
  run.clear();
  assert(run.phase == Phase.Clear);
  assert(!run.hurt() && !run.collect());
  run.tick(10);
  run.clear();
  assert(run.seconds == 23.5 && run.shards == 2 && run.lives == 3);
  run.restart();
  assert(run.phase == Phase.Playing && run.shards == 0 && run.seconds == 0);
}
