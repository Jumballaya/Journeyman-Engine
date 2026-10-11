#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../core/ecs/component/Component.hpp"

// Picks the entity's SpriteAnimation clip from its motion and the params a
// script sets: the first state whose condition holds plays (see AnimatorSystem).
struct AnimatorComponent : public Component<AnimatorComponent> {
  COMPONENT_NAME("AnimatorComponent");

  // "onGround && speed > 30", "!onGround", "hurt": params ANDed, each true when
  // nonzero or compared with a number. Empty: always.
  struct Condition {
    struct Test {
      std::string param;
      enum class Op { NonZero, Zero, Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual } op = Op::NonZero;
      float value = 0.0f;
    };
    std::vector<Test> tests;
    // nullopt (with the reason in `error`) when `text` isn't one.
    static std::optional<Condition> parse(std::string_view text, std::string& error);
  };
  struct State {
    std::string clip;
    Condition when;
    bool hold = false;  // once playing, stays until its clip finishes
  };

  std::vector<State> states;
  // Set by scripts; the motion's (vx, vy, speed, onGround, onWall) are read
  // from the entity's velocity unless a script set them. Triggers last a frame.
  std::unordered_map<std::string, float> params;
  std::vector<std::string> triggers;
  int playing = -1;  // the state shown (-1: none yet)
};
