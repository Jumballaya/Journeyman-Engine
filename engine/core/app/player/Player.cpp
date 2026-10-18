#include "../AgentTools.hpp"
#include "../DevOptions.hpp"

// An exported game: no JM_* variable drives, steers, records or dumps it, and
// scripts' Debug drawing is off. The agent tools aren't linked: the run gets
// AgentTools itself, which does nothing.
DevOptions DevOptions::fromEnvironment() {
  DevOptions o;
  o.debugDraw = false;
  return o;
}

std::unique_ptr<AgentTools> AgentTools::make(Engine&, const DevOptions&) { return std::make_unique<AgentTools>(); }
