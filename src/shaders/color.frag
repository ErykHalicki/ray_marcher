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
  float amplitude = 10.0;
  float frequency = 0.05;
  
  int octaves = 4;
  for (int i = 0; i < octaves; i++) {
	value += amplitude * perlin(p * frequency);
	frequency *= 2.0;
	amplitude *= 0.5;
  }

  return value;
}

float sceneSDF(vec3 p) {
    float terrain_height = fbm(p.xz);
    return p.y - terrain_height;
}

float ray_march(vec3 ro, vec3 rd) {
    float t = 0.0; // distance traveled along the ray

	int maxIterations = 100;
    for (int i = 0; i < maxIterations; i++) { // max steps
        vec3 p = ro + t * rd; // current position along the ray
        float d = sceneSDF(p);

        if (d < 0.001) { // hit
            return t / 100.0; //float(i / maxIterations);
        }

        t += d;

        if (t > 100.0) { // max distance
            break;
        }
    }

    return -1.0; // no hit
}

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    // Camera setup
    vec3 ro = vec3(0.0, 30.0, 50.0); // Move camera back and up
    vec3 ta = vec3(0.0, 0.0, 0.0);   // Look at the origin

    vec3 fwd = normalize(ta - ro);
    vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
    vec3 up = normalize(cross(right, fwd));

    vec2 uv = fragUV * 2.0 - 1.0;
    float fov = 1.0;
    vec3 rd = normalize(uv.x * right + uv.y * up + fov * fwd);

    // Ray march
    float t = ray_march(ro, rd);

    // Determine color
    if (t > 0.0) {
        // Hit: for now, just white
        outColor = vec4(t, t, t, 1.0);
    } else {
        // Miss: sky color
        outColor = vec4(0.5, 0.7, 1.0, 1.0);
    }
}
