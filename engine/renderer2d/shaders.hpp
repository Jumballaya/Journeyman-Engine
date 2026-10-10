#pragma once

// GL 4.1 everywhere: macOS's ceiling, and what the loader (vendor/glad) knows.
#define JM_GLSL_VERSION "#version 410 core"

inline constexpr const char* sprite_vertex_shader = R"(
)" JM_GLSL_VERSION R"(

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in mat4 a_transform;
layout(location = 6) in vec4 a_color;
layout(location = 7) in vec4 a_texRect;

uniform mat4 u_projView;

out vec2 v_texCoord;
out vec4 v_color;
out vec2 v_world;

void main() {
    gl_Position = u_projView * a_transform * a_position;  // as before lighting: the same rounding
    v_world = (a_transform * a_position).xy;

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
in vec2 v_world;

uniform sampler2D u_texture;

// Lighting (Lights.hpp): off, the color is the sprite's as is.
const int MAX_LIGHTS = 32;
uniform bool u_lit;
uniform vec3 u_ambient;
uniform int u_lightCount;
uniform vec4 u_lightPlace[MAX_LIGHTS];  // x, y, radius, falloff
uniform vec4 u_lightColor[MAX_LIGHTS];  // rgb times energy

void main() {
    vec4 color = texture(u_texture, v_texCoord) * v_color;
    if (u_lit) {
        vec3 light = u_ambient;
        for (int i = 0; i < u_lightCount; ++i) {
            float reach = 1.0 - distance(v_world, u_lightPlace[i].xy) / u_lightPlace[i].z;
            if (reach > 0.0) light += u_lightColor[i].rgb * pow(reach, u_lightPlace[i].w);  // pow(0, 0) is undefined
        }
        color.rgb *= light;
    }
    outColor = color;
}
)";

inline constexpr const char* screen_vertex_shader = R"(
)" JM_GLSL_VERSION R"(

layout(location=0) in vec3 a_position;
layout(location=1) in vec2 a_texCoord;

out vec2 v_texCoord;

// Unflipped: effects sample FBO attachments (GL bottom-up); flipping would
// turn odd-length chains upside down.
void main() {
    gl_Position = vec4(a_position, 1.0);
    v_texCoord = a_texCoord;
}
)";

// Prepended to effect/transition shaders without their own #version; the
// uniforms are documented in docs/content.md ("Shaders").
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
