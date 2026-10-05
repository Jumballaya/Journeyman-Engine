// Battle transition: the old frame (u_aux) twists into a white flash, then the
// battle (u_primary) fades in.
void main() {
  vec2 c = v_texCoord - 0.5;
  float r = length(c);
  float a = u_progress * 10.0 * max(0.0, 1.0 - r * 1.4);
  vec2 s = vec2(c.x * cos(a) - c.y * sin(a), c.x * sin(a) + c.y * cos(a)) + 0.5;
  vec4 old = mix(texture(u_aux, s), vec4(1.0), smoothstep(0.35, 0.65, u_progress));
  outColor = mix(old, texture(u_primary, v_texCoord), smoothstep(0.6, 1.0, u_progress));
}
