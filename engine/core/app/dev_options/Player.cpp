#include "../DevOptions.hpp"

// An exported game: no JM_* variable drives, steers, records or dumps it, and
// scripts' Debug drawing is off.
DevOptions DevOptions::fromEnvironment() {
  DevOptions o;
  o.debugDraw = false;
  return o;
}
