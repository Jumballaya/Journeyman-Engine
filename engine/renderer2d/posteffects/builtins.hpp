#pragma once

#include "../Renderer2D.hpp"
#include "../shaders.hpp"
#include "PostEffect.hpp"

namespace posteffects::builtins {

inline PostEffect passthrough(Renderer2D& r) {
  PostEffect effect;
  effect.shader = r.createShader(screen_vertex_shader, screen_fragment_shader);
  return effect;
}

}  // namespace posteffects::builtins
