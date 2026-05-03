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
layout(location = 8) in float a_layer;

)" JM_CAMERA_UBO R"( {
  mat4 uProj;
  mat4 uView;
  mat4 uProjView;
  vec4 uViewport; // [u, v, _, _]
};

out vec2 v_texCoord;
out vec4 v_color;
out float v_layer;

void main() {
    vec4 world = a_transform * a_position;
    
    gl_Position = uProjView * world;

    // a_texRect is [u, v, w, h] in atlas UV space (origin + size). a_uv is the
    // quad's [0,1]^2 local UV. Flip the quad's V first (the prior orientation
    // convention, so the asset upload path doesn't have to change), then map
    // the flipped quad UV into the atlas subrect. Flipping after the mapping
    // reflects each region across the atlas's vertical midline — fine for
    // default texRect=(0,0,1,1), wrong for any subrect (atlased sprites
    // sample the wrong band of the atlas). For default texRect this still
    // collapses to vec2(a_uv.x, 1.0 - a_uv.y), bit-identical to the previous
    // shader on existing scenes.
    vec2 quadUV = vec2(a_uv.x, 1.0 - a_uv.y);
    v_texCoord = quadUV * a_texRect.zw + a_texRect.xy;
    v_color = a_color;
    v_layer = a_layer;
}
)";

inline constexpr const char* sprite_fragment_shader = R"(
)" JM_GLSL_VERSION R"(

out vec4 outColor;

in vec2 v_texCoord;
in vec4 v_color;
in float v_layer;

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