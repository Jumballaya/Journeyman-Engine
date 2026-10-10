#pragma once

#include <cstdint>

// A script's fuel: every call into a script (its start, onUpdate, onOverlap, a
// message) gets a budget of steps, one per function call and loop iteration
// (wasm3's yield hook). Running out traps the script, which disables it with
// an error: a script stuck in a loop costs that script, not the whole game.
// Steps, not time, so it stops at the same point on every machine.
namespace fuel {

// The demos' busiest call (a scene's setup) takes about 420,000 steps; this is
// about 60 times that, and half a second or so of a runaway loop.
inline constexpr uint64_t kStepsPerCall = 25'000'000;

// What a script that ran out traps with.
inline constexpr const char* kOutOfFuel = "ran out of fuel: over 25 million steps in one call (an endless loop?)";

// Installs the hook; once, before any script runs.
void install();

// Fills the tank for the call about to be made.
void refill();

}  // namespace fuel
