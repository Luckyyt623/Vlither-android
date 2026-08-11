import modules.common;

struct vs_input {
  [[vk::location(0)]] float2 pos;
  [[vk::location(1)]] float3 circ;
  [[vk::location(2)]] float4 color;
  [[vk::location(3)]] float flicker;
};

struct vs_output {
  float4 clipspace : SV_Position;
  [[vk::location(0)]] float2 uv;
  [[vk::location(1)]] float4 color;
  [[vk::location(3)]] float flicker;
};

[[vk::binding(0, 0)]] ConstantBuffer<Global> global;

vs_output vs_main(vs_input input) {
  vs_output output;

  float2 p = (input.pos - 0.5) * (input.circ.z * 2 + EDGE_SOFTNESS);
  float2 r = p + 0.5 * (input.circ.z + EDGE_SOFTNESS * 0.5);

  float2 hv = global.viewport * 0.5;
  float2 wp = r + input.circ.xy;
  float2 o = (wp - hv) / hv;

  output.clipspace = float4(o, 1);
  output.uv = input.pos;
  output.color = input.color;
  output.flicker = input.flicker;

  return output;
}

float4 fs_main(vs_output input) {
  // Square signed-distance field. Using max(abs(x), abs(y)) keeps the food
  // perfectly square while still giving the edges anti-aliasing.
  float2 q = abs(input.uv * 2 - 1);
  float d = max(q.x, q.y);
  float p = max(length(float2(ddx(d), ddy(d))), 0.0001);
  float f = lerp(0.45, 0.5, input.flicker);
  float a = smoothstep(0.0, EDGE_SOFTNESS, (f - d) / p) * 0.82;
  float glow = max(0.0, f - d) * input.flicker * 0.8;

  return float4(input.color.rgb + glow, input.color.a * a);
}
