// Damage bookkeeping only. Invulnerability, effects, scoring and death
// sequences belong to the caller. A lethal hit is reported exactly once.
export class Health {
  private value: f32;
  readonly max: f32;
  constructor(maximum: f32) { this.max = Mathf.max(0, maximum); this.value = this.max; }
  get current(): f32 { return this.value; }
  get fraction(): f32 { return this.max > 0 ? this.value / this.max : 0; }
  get dead(): bool { return this.value <= 0; }
  damage(amount: f32): bool {
    if (this.dead || amount <= 0) return false;
    this.value = Mathf.max(0, this.value - amount);
    return this.dead;
  }
  heal(amount: f32): void { this.value = Mathf.min(this.max, this.value + Mathf.max(0, amount)); }
}
