import modules.common;

struct vs_input {
  [[vk::location(0)]] float2 pos;
  [[vk::location(1)]] float4 circ;
  [[vk::location(2)]] float4 uv;
  [[vk::location(3)]] float4 color;
  [[vk::location(4)]] float2 shape;
};

struct vs_output {
  float4 clipspace : SV_Position;
  [[vk::location(0)]] float2 uv;
  [[vk::location(1)]] float2 uv2;
  [[vk::location(2)]] float4 color;
  [[vk::location(3)]] float2 dimensions;
  [[vk::location(4)]] float2 shape;
};

[[vk::binding(0, 0)]] ConstantBuffer<Global> global;
[[vk::binding(1, 0)]] Sampler2D tex_atlas;

vs_output vs_main(vs_input input) {
  vs_output output;

  bool is_capsule = input.shape.y > 0.5;
  float2 dimensions = is_capsule
                          ? float2(input.circ.z, input.shape.x)
                          : float2(input.circ.z, input.circ.z);
  float2 p = (input.pos - 0.5) * (dimensions + EDGE_SOFTNESS);

  float s = sin(input.circ.w);
  float c = cos(input.circ.w);

  float2 r;
  if (is_capsule) {
    r = float2(p.x * c - p.y * s, p.x * s + p.y * c) +
        0.5 * dimensions;
  } else {
    // Preserve the original circle-sprite positioning exactly.
    r = float2(p.x * c - p.y * s, p.x * s + p.y * c) +
        0.5 * (input.circ.z + EDGE_SOFTNESS * 0.5);
  }

  float2 hv = global.viewport * 0.5;
  float2 wp = r + input.circ.xy;
  float2 o = (wp - hv) / hv;

  output.clipspace = float4(o, 1);
  output.uv = input.pos;
  output.uv2 = input.uv.xy + input.pos * input.uv.zw;
  output.color = input.color;
  output.dimensions = dimensions;
  output.shape = input.shape;

  return output;
}

float4 fs_main(vs_output input) {
  if (input.shape.y > 0.5) {
    float2 p = (input.uv - 0.5) * (input.dimensions + EDGE_SOFTNESS);
    float radius = input.shape.x * 0.5;
    float segment_half =
        max(0.0, (input.dimensions.x - input.shape.x) * 0.5);
    float2 q = float2(max(abs(p.x) - segment_half, 0.0), p.y);
    float distance_to_edge = length(q) - radius;
    float aa = max(length(float2(ddx(distance_to_edge), ddy(distance_to_edge))), 0.75);
    float alpha = 1.0 - smoothstep(-aa, aa, distance_to_edge);
    return float4(input.color.rgb, input.color.a * alpha);
  }

  float4 c = tex_atlas.Sample(input.uv2).rgba;
  float d = length(2 * input.uv - 1);
  float p = length(float2(ddx(d), ddy(d)));
  float a = smoothstep(0.0, EDGE_SOFTNESS, (1 - d) / p);
  return input.color * float4(c.rgb, a * c.a);
}
