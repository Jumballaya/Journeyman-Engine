#pragma once

#ifdef __APPLE__
  #define JM_GLSL_VERSION "#version 410 core"
  #define JM_CAMERA_UBO "layout(std140) uniform Camera"
#else
  #define JM_GLSL_VERSION "#version 460 core"
  #define JM_CAMERA_UBO "layout(std140, binding = 0) uniform Camera"
#endif

inline constexpr const char* sprite_vertex_shader = R"(
)" JM_GLSL_VERSION R"(

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in mat4 a_transform;
layout(location = 6) in vec4 a_color;
layout(location = 7) in vec4 a_texRect;

)" JM_CAMERA_UBO R"( {
  mat4 uProj;
  mat4 uView;
  mat4 uProjView;
  vec4 uViewport; // [u, v, _, _]
};

out vec2 v_texCoord;
out vec4 v_color;

void main() {
    vec4 world = a_transform * a_position;
    
    gl_Position = uProjView * world;

    // Flip V before mapping into the texRect; flipping after would mirror
    // every atlas region across the atlas midline.
    vec2 quadUV = vec2(a_uv.x, 1.0 - a_uv.y);
    v_texCoord = quadUV * a_texRect.zw + a_texRect.xy;
    v_color = a_color;
}
)";

inline constexpr const char* sprite_fragment_shader = R"(
)" JM_GLSL_VERSION R"(

out vec4 outColor;

in vec2 v_texCoord;
in vec4 v_color;

uniform sampler2D u_texture;

void main() {
    outColor = texture(u_texture, v_texCoord) * v_color;
}
)";

inline constexpr const char* screen_vertex_shader = R"(
)" JM_GLSL_VERSION R"(

layout(location=0) in vec3 a_position;
layout(location=1) in vec2 a_texCoord;


out vec2 v_texCoord;

// Post-effect vertex shader. v_texCoord is passed through unflipped because
// the sampled texture is an FBO color attachment (stored bottom-up in GL's
// native orientation). Flipping here would invert the frame on every effect
// pass, producing an upside-down image for odd-count chains.
void main() {
    gl_Position = vec4(a_position, 1.0);
    v_texCoord = a_texCoord;
}
)";

inline constexpr const char* screen_fragment_shader = R"(
)" JM_GLSL_VERSION R"(

out vec4 outColor;

in vec2 v_texCoord;

uniform sampler2D u_texture;

void main() {
    outColor = texture(u_texture, v_texCoord);
}
)";
// Prepended to every post-effect / transition shader that doesn't declare its
// own #version. Authors write only `void main()` (plus helpers). Inputs:
//   u_primary   the frame so far (for transitions: the incoming scene)
//   u_aux       transitions: the outgoing scene's last frame
//   u_progress  transitions: 0 = all old scene, 1 = all new scene
//   u_resolution  render target size in pixels
//   u_viewport    letterboxed game area in pixels (x, y, w, h)
//   u_logical     game's logical resolution (e.g. 480x640)
//   u_time        seconds since start
inline constexpr const char* post_effect_prelude = JM_GLSL_VERSION R"(
in vec2 v_texCoord;
out vec4 outColor;
uniform sampler2D u_primary;
uniform sampler2D u_aux;
uniform float u_progress;
uniform vec2 u_resolution;
uniform vec4 u_viewport;
uniform vec2 u_logical;
uniform float u_time;
#line 1
)";
