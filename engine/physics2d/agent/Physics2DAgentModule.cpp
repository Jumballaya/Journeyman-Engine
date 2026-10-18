#include <cmath>
#include <sstream>

#include "../../core/app/Engine.hpp"
#include "../../core/app/Registration.hpp"
#include "../Colliders.hpp"
#include "../Queries.hpp"

// The driver's `near tag=Name [distance]` (the agent build links this, the
// player doesn't): what's within distance of its colliders, nearest first.
class Physics2DAgentModule : public EngineModule {
 public:
  void initialize(Engine&) override {}
  void shutdown(Engine&) override {}
  bool driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) override;
  const char* name() const override { return "Physics2DAgentModule"; }
};

REGISTER_MODULE(Physics2DAgentModule)

bool Physics2DAgentModule::driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) {
  if (verb != "near") return false;
  std::istringstream words{std::string(args)};
  std::string tag, distance, extra;
  words >> tag >> distance >> extra;
  float within = 4.0f;
  bool ok = tag.starts_with("tag=") && extra.empty();
  if (ok && !distance.empty()) {
    std::istringstream number(distance);
    ok = (number >> within) && number.eof() && within >= 0.0f;
  }
  if (!ok) {
    reply = {{"ok", false}, {"error", "near takes tag=Name and a distance (default 4), e.g. near tag=Player 2"}};
    return true;
  }
  tag = tag.substr(4);
  World& world = app.getWorld();
  bool hasCollider = false;
  forEachCollider(world, [&](const Collider& c) { hasCollider = hasCollider || world.hasTag(c.entity, tag); });
  if (!hasCollider) {
    reply = {{"ok", false}, {"error", "no entity tagged " + tag + " has a box or circle collider"}};
    return true;
  }
  nlohmann::json near = nlohmann::json::array();
  for (const Nearby& n : nearby(world, tag, within))
    near.push_back({{"tags", world.tagNames(n.entity)}, {"kind", n.kind}, {"gap", std::round(static_cast<double>(n.gap) * 1000.0) / 1000.0}});
  reply = {{"ok", true}, {"near", std::move(near)}};
  return true;
}
