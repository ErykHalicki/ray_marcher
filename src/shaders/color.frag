#version 450

float rand(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233)) * 43758.5453));
}

vec2 rand_vec2(vec2 p) {
  vec2 v = fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);
  return v * 2.0 - 1.0;  // Map from [0,1] to [-1,1]
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

float fbm(vec2 p) {
  float value = 0.0;
  float amplitude = 5.0;
  float frequency = 10.0;
  
  int octaves = 16;
  for (int i = 0; i < octaves; i++) {
	value += amplitude * perlin(p * frequency);
	frequency *= 5.0;
	amplitude *= 0.5;
  }

  return value;
}

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    float noise = fbm(fragUV); 
    outColor = vec4(noise, noise, noise, 1.0);
}
