// Retro CRT: scanlines on the game's logical pixel rows, slight RGB
// separation, and a soft vignette. Uniform u_strength (0..1) scales it all.
uniform float u_strength;

void main() {
  vec2 uv = v_texCoord;
  vec2 local = (gl_FragCoord.xy - u_viewport.xy) / u_viewport.zw;   // 0..1 over the game area
  float shift = 0.6 / u_resolution.x * u_strength;
  vec3 c;
  c.r = texture(u_primary, uv + vec2(shift, 0.0)).r;
  c.g = texture(u_primary, uv).g;
  c.b = texture(u_primary, uv - vec2(shift, 0.0)).b;

  // Darken the seam between logical pixel rows.
  float row = fract(local.y * u_logical.y);
  float seam = 1.0 - smoothstep(0.0, 0.3, min(row, 1.0 - row));
  float scan = 1.0 - 0.18 * u_strength * seam;

  vec2 p = local - 0.5;
  float vignette = mix(1.0, smoothstep(0.85, 0.35, length(p * vec2(1.0, 0.85))), 0.55 * u_strength);

  outColor = vec4(c * scan * vignette * (1.0 + 0.06 * u_strength), 1.0);
}
