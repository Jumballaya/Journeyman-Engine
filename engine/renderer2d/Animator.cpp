#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>

#include "../physics2d/VelocityComponent.hpp"
#include "AnimatorSystem.hpp"
#include "SpriteAnimationComponent.hpp"

namespace {

using Test = AnimatorComponent::Condition::Test;

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

bool isName(std::string_view s) {
  if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
  for (char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

// "name", "!name" or "name <op> number".
std::optional<Test> parseTest(std::string_view text) {
  text = trim(text);
  if (!text.empty() && text[0] == '!') {
    const std::string_view name = trim(text.substr(1));
    return isName(name) ? std::optional(Test{std::string(name), Test::Op::Zero}) : std::nullopt;
  }
  static constexpr std::pair<std::string_view, Test::Op> kOps[] = {
      {"<=", Test::Op::LessEqual}, {">=", Test::Op::GreaterEqual}, {"==", Test::Op::Equal},
      {"!=", Test::Op::NotEqual},  {"<", Test::Op::Less},          {">", Test::Op::Greater}};
  for (const auto& [symbol, op] : kOps) {
    const size_t at = text.find(symbol);
    if (at == std::string_view::npos) continue;
    const std::string_view name = trim(text.substr(0, at)), number = trim(text.substr(at + symbol.size()));
    const std::string digits(number);
    char* end = nullptr;
    const float value = std::strtof(digits.c_str(), &end);
    if (!isName(name) || digits.empty() || end != digits.c_str() + digits.size()) return std::nullopt;
    return Test{std::string(name), op, value};
  }
  return isName(text) ? std::optional(Test{std::string(text), Test::Op::NonZero}) : std::nullopt;
}

bool passes(const Test& t, float v) {
  switch (t.op) {
    case Test::Op::NonZero: return v != 0.0f;
    case Test::Op::Zero: return v == 0.0f;
    case Test::Op::Less: return v < t.value;
    case Test::Op::LessEqual: return v <= t.value;
    case Test::Op::Greater: return v > t.value;
    case Test::Op::GreaterEqual: return v >= t.value;
    case Test::Op::Equal: return v == t.value;
    case Test::Op::NotEqual: return v != t.value;
  }
  return false;
}

// A param's value: a script's, else the motion's, else 0.
float param(const AnimatorComponent& animator, const VelocityComponent* velocity, const std::string& name) {
  if (auto it = animator.params.find(name); it != animator.params.end()) return it->second;
  if (!velocity) return 0.0f;
  if (name == "vx") return velocity->velocity.x;
  if (name == "vy") return velocity->velocity.y;
  if (name == "speed") return std::hypot(velocity->velocity.x, velocity->velocity.y);
  if (name == "onGround") return velocity->blocked.y < 0.0f ? 1.0f : 0.0f;
  if (name == "onWall") return velocity->blocked.x != 0.0f ? 1.0f : 0.0f;
  return 0.0f;
}

}  // namespace

std::optional<AnimatorComponent::Condition> AnimatorComponent::Condition::parse(std::string_view text, std::string& error) {
  Condition condition;
  if (trim(text).empty()) return condition;
  for (size_t start = 0;;) {
    const size_t end = text.find("&&", start);
    auto test = parseTest(text.substr(start, end == std::string_view::npos ? end : end - start));
    if (!test) {
      error = "expected params joined by &&, each `name`, `!name` or `name > 30` (< <= > >= == !=)";
      return std::nullopt;
    }
    condition.tests.push_back(std::move(*test));
    if (end == std::string_view::npos) return condition;
    start = end + 2;
  }
}

void AnimatorSystem::update(World& world, float) {
  for (auto [entity, animator, sprite] : world.view<AnimatorComponent, SpriteAnimationComponent>()) {
    const auto* velocity = world.getComponent<VelocityComponent>(entity);
    const auto& states = animator->states;
    const int playing = animator->playing;
    // Held: its clip still on and running (a script's sprite.play ends it).
    if (playing >= 0 && playing < static_cast<int>(states.size()) && states[playing].hold && !sprite->finished &&
        sprite->current == states[playing].clip) {
      continue;  // triggers wait for the next look
    }
    for (int i = 0; i < static_cast<int>(states.size()); ++i) {
      const auto& tests = states[i].when.tests;
      if (!std::all_of(tests.begin(), tests.end(), [&](const Test& t) { return passes(t, param(*animator, velocity, t.param)); })) continue;
      animator->playing = i;
      if (states[i].hold) sprite->restart(states[i].clip);  // from its first frame, even if just shown
      else sprite->play(states[i].clip);
      break;
    }
    for (const std::string& name : animator->triggers) animator->params.erase(name);
    animator->triggers.clear();
  }
}
