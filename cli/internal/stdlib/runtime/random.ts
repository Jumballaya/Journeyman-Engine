// Uses the engine-seeded AssemblyScript random source.
export class Random {
  static range(min: f32, max: f32): f32 { return min + <f32>Math.random() * (max - min); }
  // Both endpoints are included; inverted ranges are a programming error.
  static int(min: i32, max: i32): i32 {
    assert(max >= min, "Random.int: inverted range");
    return <i32>(<f64>min + Math.floor(Math.random() * (<f64>max - <f64>min + 1)));
  }
  static chance(probability: f32): bool { return <f32>Math.random() < probability; }
  static pick<T>(items: T[]): T {
    assert(items.length > 0, "Random.pick: empty array");
    return items[Random.int(0, items.length - 1)];
  }
}
