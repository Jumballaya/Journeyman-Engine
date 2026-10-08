#include "Fuel.hpp"

#include <wasm3.h>

namespace fuel {
namespace {

uint64_t gLeft = kStepsPerCall;

M3Result burn() { return gLeft-- == 0 ? kOutOfFuel : m3Err_none; }

}  // namespace

void install() { m3_SetYieldHook(&burn); }

void refill() { gLeft = kStepsPerCall; }

}  // namespace fuel
