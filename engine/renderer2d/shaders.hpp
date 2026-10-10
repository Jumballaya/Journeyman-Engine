#pragma once

// GL 4.1 everywhere: macOS's ceiling, and what the loader (vendor/glad) knows.
#define JM_GLSL_VERSION "#version 410 core"
// Lights the sprite shader takes; directions per light in the shadow map (Shadows.hpp).
#define JM_MAX_LIGHTS 32
#define JM_SHADOW_ANGLES 1024
#define JM_STRINGIFY(x) #x
#define JM_SHADER_INT(x) JM_STRINGIFY(x)

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
out vec2 v_axisX;  // the sprite's x and y in the world: turn its normal map
out vec2 v_axisY;

void main() {
    gl_Position = u_projView * a_transform * a_position;  // as before lighting: the same rounding
    v_world = (a_transform * a_position).xy;

    // Flip V before mapping into the texRect; flipping after would mirror
    // every atlas region across the atlas midline.
    vec2 quadUV = vec2(a_uv.x, 1.0 - a_uv.y);
    v_texCoord = quadUV * a_texRect.zw + a_texRect.xy;
    v_color = a_color;
    v_axisX = normalize(a_transform[0].xy);  // mirrored (negative scale x): the normal's x flips too
    v_axisY = normalize(a_transform[1].xy);
}
)";

inline constexpr const char* sprite_fragment_shader = R"(
)" JM_GLSL_VERSION R"(

out vec4 outColor;

in vec2 v_texCoord;
in vec4 v_color;
in vec2 v_world;
in vec2 v_axisX;
in vec2 v_axisY;

uniform sampler2D u_texture;
uniform sampler2D u_normal;  // tangent space, y up; only if u_hasNormal
uniform bool u_hasNormal;

// Lighting (Lights.hpp): off, the color is the sprite's as is.
const int MAX_LIGHTS = )" JM_SHADER_INT(JM_MAX_LIGHTS) R"(;
uniform bool u_lit;
uniform vec3 u_ambient;
uniform int u_lightCount;
uniform vec4 u_lightPlace[MAX_LIGHTS];  // x, y, radius, falloff
uniform vec4 u_lightColor[MAX_LIGHTS];  // rgb times energy, height

// Shadows (Shadows.hpp): row i of u_shadowMap is light i's distance to the nearest occluder, by angle.
const int SHADOW_ANGLES = )" JM_SHADER_INT(JM_SHADOW_ANGLES) R"(;
uniform bool u_shadows;
uniform sampler2D u_shadowMap;
uniform float u_lightShadow[MAX_LIGHTS];  // softness; below 0: casts none

// Whether light i reaches distance d along the column (wrapped around).
float reaches(int i, int column, float d) {
    return step(d, texelFetch(u_shadowMap, ivec2(column & (SHADOW_ANGLES - 1), i), 0).r + 1.0);
}

// How much of light i reaches a pixel `to` away: 5 taps, wider farther out,
// each blending its two nearest columns so the penumbra has no steps.
float shadowed(int i, vec2 to) {
    float d = length(to);
    float column = (atan(to.y, to.x) / 6.2831853 + 0.5) * float(SHADOW_ANGLES) - 0.5;
    float spread = u_lightShadow[i] * (0.5 + 2.0 * d / u_lightPlace[i].z);  // texels
    const float weight[5] = float[](1.0, 4.0, 6.0, 4.0, 1.0);
    float lit = 0.0;
    for (int k = 0; k < 5; ++k) {
        float x = column + float(k - 2) * spread;
        int c = int(floor(x));
        lit += weight[k] * mix(reaches(i, c, d), reaches(i, c + 1, d), x - floor(x));
    }
    return lit / 16.0;
}

void main() {
    vec4 color = texture(u_texture, v_texCoord) * v_color;
    if (u_lit) {
        vec3 n = vec3(0.0, 0.0, 1.0);
        if (u_hasNormal) {
            vec3 t = texture(u_normal, v_texCoord).xyz * 2.0 - 1.0;
            n = normalize(vec3(t.x * v_axisX + t.y * v_axisY, t.z));
        }
        vec3 light = u_ambient;
        for (int i = 0; i < u_lightCount; ++i) {
            float reach = 1.0 - distance(v_world, u_lightPlace[i].xy) / u_lightPlace[i].z;
            if (reach <= 0.0) continue;  // pow(0, 0) is undefined
            float k = pow(reach, u_lightPlace[i].w);
            if (u_hasNormal) k *= max(dot(n, normalize(vec3(u_lightPlace[i].xy - v_world, u_lightColor[i].w))), 0.0);
            if (u_shadows && u_lightShadow[i] >= 0.0) k *= shadowed(i, v_world - u_lightPlace[i].xy);
            light += u_lightColor[i].rgb * k;
        }
        color.rgb *= light;
    }
    outColor = color;
}
)";

// The shadow pass: a quad per ShadowCaster over its row's span; each texel
// keeps the distance to its segment (blended by MIN: the nearest).
inline constexpr const char* shadow_vertex_shader = R"(
)" JM_GLSL_VERSION R"(

layout(location = 0) in vec4 a_segment;
layout(location = 1) in vec2 a_light;
layout(location = 2) in vec2 a_span;
layout(location = 3) in float a_row;

flat out vec4 v_segment;
flat out vec2 v_light;

void main() {
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);  // a strip: (0,0) (1,0) (0,1) (1,1)
    float x = mix(a_span.x, a_span.y, corner.x) / 3.14159265;
    float y = (a_row + corner.y) / float()" JM_SHADER_INT(JM_MAX_LIGHTS) R"() * 2.0 - 1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
    v_segment = a_segment;
    v_light = a_light;
}
)";

inline constexpr const char* shadow_fragment_shader = R"(
)" JM_GLSL_VERSION R"(

flat in vec4 v_segment;
flat in vec2 v_light;
out float outDistance;

float cross2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

// As shadowDistance in Shadows.cpp.
void main() {
    float angle = gl_FragCoord.x / float()" JM_SHADER_INT(JM_SHADOW_ANGLES) R"() * 6.2831853 - 3.14159265;
    vec2 dir = vec2(cos(angle), sin(angle));
    vec2 toA = v_segment.xy - v_light, e = v_segment.zw - v_segment.xy;
    float denom = cross2(dir, e);
    float t = abs(denom) < 1e-6 ? min(length(toA), length(toA + e)) : cross2(toA, e) / denom;
    if (t < 0.0) discard;
    outDistance = t;
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
