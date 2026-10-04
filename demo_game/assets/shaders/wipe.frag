// Scene transition: a diagonal wipe of chunky pixel blocks with a bright
// leading edge. u_aux = old scene, u_primary = new scene, u_progress 0 -> 1.
void main() {
  vec2 local = (gl_FragCoord.xy - u_viewport.xy) / u_viewport.zw;
  vec2 block = floor(local * u_logical / 16.0);                // 16 logical px blocks
  vec2 blocks = u_logical / 16.0;
  float d = (block.x / blocks.x + (1.0 - block.y / blocks.y)) * 0.5;   // 0 at top-left
  float edge = u_progress * 1.25 - 0.125;
  vec4 oldC = texture(u_aux, v_texCoord);
  vec4 newC = texture(u_primary, v_texCoord);
  float t = smoothstep(edge - 0.06, edge + 0.06, d);
  vec4 c = mix(newC, oldC, t);
  float glow = 1.0 - smoothstep(0.0, 0.05, abs(d - edge));
  outColor = vec4(mix(c.rgb, vec3(1.0, 0.85, 0.35), glow * 0.6), 1.0);
}
