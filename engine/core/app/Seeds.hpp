#pragma once

#include <cstdint>

// A run's randomness: every random number derives from one seed, handed out
// in the order it's asked for (each script's Math.random, camera shake...).
// The engine runs in a fixed order, so the same seed repeats the run.
class Seeds {
 public:
  explicit Seeds(uint64_t seed) : _seed(seed), _state(seed) {}

  uint64_t seed() const { return _seed; }

  // The next of the run's seeds (SplitMix64: well mixed even for seeds 1, 2, 3).
  uint64_t next() {
    uint64_t z = (_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

 private:
  uint64_t _seed;
  uint64_t _state;
};
