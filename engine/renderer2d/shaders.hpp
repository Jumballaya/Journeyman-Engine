#pragma once

inline constexpr const char* sprite_vertex_shader = R"(
#version 460 core

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in mat4 a_transform;
layout(location = 6) in vec4 a_color;
layout(location = 7) in vec4 a_texRect;
layout(location = 8) in float a_layer;

layout(std140, binding = 0) uniform Camera {
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

    v_texCoord = a_uv;
    v_texCoord.y = 1.0 - v_texCoord.y;
    v_color = a_color;
    v_layer = a_layer;
}
)";

inline constexpr const char* sprite_fragment_shader = R"(
#version 460 core

out vec4 outColor;

in vec2 v_texCoord;
in vec4 v_color;
in float v_layer;

uniform sampler2D u_texture;

void main() {
    // outColor = texture(u_texture, v_texCoord) * v_color;
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

inline constexpr const char* screen_vertex_shader = R"(
#version 460 core

layout(location=0) in vec3 a_position;
layout(location=1) in vec2 a_texCoord;


out vec2 v_texCoord;

void main() {
    gl_Position = vec4(a_position, 1.0);
    v_texCoord = a_texCoord;
    v_texCoord.y = 1.0 - v_texCoord.y;
}
)";

inline constexpr const char* screen_fragment_shader = R"(
#version 460 core

out vec4 outColor;

in vec2 v_texCoord;

uniform sampler2D u_texture;

void main() {
    outColor = texture(u_texture, v_texCoord);
}
)";