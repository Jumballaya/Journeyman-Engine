// Scene transition: the old scene breaks into 8px blocks that vanish in a
// random order (through black at the midpoint).
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  vec2 local = (gl_FragCoord.xy - u_viewport.xy) / u_viewport.zw;
  float h = hash(floor(local * u_logical / 8.0));
  vec4 oldC = texture(u_aux, v_texCoord);
  vec4 newC = texture(u_primary, v_texCoord);
  float outT = smoothstep(h - 0.1, h, u_progress * 2.0);            // old blocks fade first half
  float inT = smoothstep(h - 0.1, h, u_progress * 2.0 - 1.0);      // new blocks in second half
  vec3 c = mix(oldC.rgb, vec3(0.02, 0.03, 0.06), outT);
  c = mix(c, newC.rgb, inT);
  outColor = vec4(c, 1.0);
}
