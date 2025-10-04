#version 450

float rand(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233)) * 43758.5453));
}

vec2 rand_vec2(vec2 p) {
  return vec2(p.x * rand(p), p.y * rand(p));
}

float perlin(vec2 p) {
  vec2 ip = floor(p);
  vec2 fp = fract(p);

  float top_left = dot(rand_vec2(ip + vec2(0.0, 1.0)), fp - vec2(0.0, 1.0));
  float top_right = dot(rand_vec2(ip + vec2(1.0, 1.0)), fp - vec2(1.0, 1.0));
  float bottom_left = dot(rand_vec2(ip), fp);
  float bottom_right = dot(rand_vec2(ip + vec2(1.0, 0.0)), fp - vec2(1.0, 0.0));

  vec2 s = smoothstep(0.0, 1.0, fp);
  return mix(mix(bottom_left, bottom_right, s.x), mix(top_left, top_right, s.x), s.y);
}

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    float noise = perlin(fragUV * 10.0); // Scale the input to see more detail
    outColor = vec4(noise, noise, noise, 1.0);
}
