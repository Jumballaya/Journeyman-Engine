export enum Phase { Playing, Clear, GameOver }

export class Run {
  shards: i32 = 0;
  lives: i32 = 3;
  seconds: f32 = 0;
  phase: Phase = Phase.Playing;

  tick(dt: f32): void {
    if (this.phase == Phase.Playing) this.seconds += Mathf.max(0, dt);
  }

  collect(): bool {
    if (this.phase != Phase.Playing) return false;
    this.shards++;
    return true;
  }

  hurt(): bool {
    if (this.phase != Phase.Playing) return false;
    this.lives--;
    if (this.lives == 0) this.phase = Phase.GameOver;
    return true;
  }

  clear(): void {
    if (this.phase == Phase.Playing) this.phase = Phase.Clear;
  }

  restart(): void {
    this.shards = 0;
    this.lives = 3;
    this.seconds = 0;
    this.phase = Phase.Playing;
  }
}
