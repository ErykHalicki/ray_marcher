#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 fragColor;

// Camera parameters from CPU
layout(set = 2, binding = 0) readonly buffer CameraParams {
    float pos_x;
    float pos_y;
    float pos_z;
    float yaw;
    float pitch;
    float time;
    float padding[2];
} camera;

// Audio parameters from CPU
layout(set = 2, binding = 1) readonly buffer AudioParams {
    float bass;
    float mid;
    float high;
	float smoothed_bass;
} audio;

// Color parameters from CPU
layout(set = 2, binding = 2) readonly buffer ColorParams {
    float max_color_distance;
    float saturation;
    float brightness;
    float num_stops;
    // Gradient stops (position, hue) pairs, max 8 stops
    vec2 stops[8];
} color_config;

// Constants
#define PI 3.14159
#define PI2 6.28318
#define HFPI 1.57079
#define EPSILON 1e-10

// Exposed variables
const int u_max_steps = 200;
const float u_max_distance = 100.0;
const float u_fog = 0.5;
const float u_specular = 0.3;
const float u_light_e_w = 0.5;

// Cubic fade (C1 smooth) - more performant
vec2 cubicInterpolation(vec2 t)
{
    return t*t*(3.0 - 2.0*t);
}

// Random hash
vec2 hash2(vec2 p)
{
    // Add slowly changing time offset to create gradual changes
    float timeOffset = camera.time * 0.00005;
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p + timeOffset) * 43758.5453123);
}

float hash1(inout float seed)
{
    return fract(sin(seed++)*43758.5453123);
}

// 2D Perlin (gradient) - Optimized
float perlinNoise(vec2 P)
{
    vec2 Pi = floor(P);
    vec2 Pf = P - Pi;

    // Get gradients from hash function (no normalization)
    vec2 g00 = hash2(Pi + vec2(0.0, 0.0));
    vec2 g10 = hash2(Pi + vec2(1.0, 0.0));
    vec2 g01 = hash2(Pi + vec2(0.0, 1.0));
    vec2 g11 = hash2(Pi + vec2(1.0, 1.0));

    // Calculate noise contributions from each corner
    float n00 = dot(g00, Pf - vec2(0.0, 0.0));
    float n10 = dot(g10, Pf - vec2(1.0, 0.0));
    float n01 = dot(g01, Pf - vec2(0.0, 1.0));
    float n11 = dot(g11, Pf - vec2(1.0, 1.0));

    // Interpolate using cubic fade
    vec2 u = cubicInterpolation(Pf);
    float nx0 = mix(n00, n10, u.x);
    float nx1 = mix(n01, n11, u.x);
    float nxy = mix(nx0, nx1, u.y);

    return nxy*0.5+0.5;
}

// Fractional Brownian Motion
float fbm(in vec2 uv, vec3 camPos)
{
    float value = 0.;
    float amplitude = 1.6;
    float freq = 1.0;

    for (int i = 0; i < 8; i++)
    {
        float multiplier = 1.0;
        //if(i < 3) {multiplier =  1.0 + audio.bass*0.2;}
        //if(i > 6) {multiplier =  0.75 + audio.high*0.4;}
        value += perlinNoise(uv * freq ) * amplitude * multiplier;
        amplitude *= 0.4;
        freq *= 2.0;
    }

    return value;
}

float terrainHeightMap(in vec3 uv, in vec3 camPos)
{
    float height = fbm(uv.xz*0.5, camPos);

    // Calculate distance from camera (horizontal distance only for consistent height zones)
    vec2 camPosXZ = vec2(camPos.x, camPos.z);
    vec2 terrainPosXZ = vec2(uv.x, uv.z);
    float distanceFromCamera = length(terrainPosXZ - camPosXZ);

    // Define distance ranges for each frequency band
    // Close range (0-7): High frequencies (treble) - nearby mountains
    // Mid range (7-14): Mid frequencies - middle distance mountains
    // Far range (14+): Bass frequencies - distant mountains

    float audioMultiplier = 0.0;

    // Close mountains - treble (high frequencies)
    float closeWeight = smoothstep(u_max_distance / 3, 0.0, distanceFromCamera);
    audioMultiplier += closeWeight * audio.high * 2.5;

    // Mid-range mountains - mid frequencies
    float midWeight = smoothstep(0.0, u_max_distance / 3, distanceFromCamera) * smoothstep(u_max_distance*2 / 3, u_max_distance / 3, distanceFromCamera);
    audioMultiplier += midWeight * audio.mid * 1.5;

    // Far mountains - bass
    float farWeight = smoothstep(u_max_distance / 3, u_max_distance*2 / 3, distanceFromCamera);
    audioMultiplier += farWeight * audio.bass * 1.5;

	audioMultiplier *= min(0.25, distance(vec2(camPos.x, camPos.z), terrainPosXZ) / 8.);

    // Apply audio modulation to height
    height *= (1.0 + audioMultiplier);

    return height ;
}

vec3 stepCountCostColor(float bias)
{
    vec3 offset = vec3(0.938, 0.328, 0.718);
    vec3 amplitude = vec3(0.902, 0.4235, 0.1843);
    vec3 frequency = vec3(0.7098, 0.7098, 0.0824);
    vec3 phase = vec3(2.538, 2.478, 0.168);

    return offset + amplitude*cos(PI2*(frequency*bias+phase));
}

vec3 getNormal(vec3 rayTerrainIntersection, float t, vec3 camPos)
{
    vec3 eps = vec3(.001 * t, .0, .0);
    vec3 n = vec3(terrainHeightMap(rayTerrainIntersection - eps.xyy, camPos) - terrainHeightMap(rayTerrainIntersection + eps.xyy, camPos),
                2. * eps.x,
                terrainHeightMap(rayTerrainIntersection - eps.yyx, camPos) - terrainHeightMap(rayTerrainIntersection + eps.yyx, camPos));

    return normalize(n);
}

mat3 computeLookAtMatrix(vec3 cameraOrigin, vec3 target, float roll)
{
    vec3 rr = vec3(sin(roll), cos(roll), 0.0);
    vec3 ww = normalize(target - cameraOrigin);
    vec3 uu = normalize(cross(ww, rr));
    vec3 vv = normalize(cross(uu, ww));

    return mat3(uu, vv, ww);
}

mat3 computeViewMatrix(float yaw, float pitch)
{
    // Create rotation matrix from yaw and pitch
    float cy = cos(yaw);
    float sy = sin(yaw);
    float cp = cos(pitch);
    float sp = sin(pitch);

    // Right vector
    vec3 right = vec3(cy, 0.0, -sy);

    // Up vector (accounting for pitch)
    vec3 up = vec3(sy * sp, cp, cy * sp);

    // Forward vector
    vec3 forward = vec3(sy * cp, sp, cy * cp);

    return mat3(right, up, forward);
}

vec3 toLinear(vec3 inputColor)
{
    inputColor.x = pow(inputColor.x, 2.2);
    inputColor.y = pow(inputColor.y, 2.2);
    inputColor.z = pow(inputColor.z, 2.2);
    return inputColor;
}

vec3 tosRGB(vec3 inputColor)
{
    inputColor.x = pow(inputColor.x, 1.0/2.2);
    inputColor.y = pow(inputColor.y, 1.0/2.2);
    inputColor.z = pow(inputColor.z, 1.0/2.2);
    return inputColor;
}

// HSV to RGB conversion
vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec2 rayMarching(in vec3 rayOrigin, in vec3 rayDirection, in float minDistance, in float maxDistance, inout vec3 intPos, inout float seed)
{
    float intersectionDistance = minDistance;
    float finalStepCount = 1.0;
    float MAX_HEIGHT = 10.0;

    for(int i = 0; i < u_max_steps; i++)
    {
        vec3 pos = rayOrigin + intersectionDistance*rayDirection;
        float height = pos.y - terrainHeightMap(pos, rayOrigin);
        if(abs(height) < (0.01 * intersectionDistance) || intersectionDistance > maxDistance)
        {
            finalStepCount = float(i);
            intPos = pos;
            break;
        }
        if(pos.y > MAX_HEIGHT)
        {
            finalStepCount = -1.0;
            intersectionDistance = -1.0;
            break;
        }
        intersectionDistance += (0.35 + hash1(seed)) * height;
    }

    return vec2(intersectionDistance, finalStepCount);
}

vec3 computeShading(vec3 terrainColor, vec3 lightColor, vec3 normal, vec3 lightDirection, vec3 viewDirection, vec3 skyColor, float terrainHeight)
{
    vec3 halfVector = normalize(lightDirection + viewDirection);
    float NdH = max(dot(normal, halfVector), 0.0);
    float NdL = max(dot(normal, lightDirection), 0.0);

    vec3 diffuse = (terrainColor / PI) * NdL;
    vec3 ambient = vec3(normal.y * 0.1) * skyColor;

    float specularIntensity = mix(u_specular * 0.2, u_specular, terrainHeight);
    vec3 specular = lightColor * pow(NdH, 10.0) * specularIntensity * NdL;

    return (diffuse + ambient + specular);
}

// Generate stars based on ray direction
float generateStars(vec3 rayDir)
{
    // Create grid cell coordinates
    vec3 starCoord = rayDir * 200.0;
    vec3 cellId = floor(starCoord);

    // Hash function for star position within cell
    vec3 hash = fract(sin(cellId * vec3(127.1, 311.7, 74.7)) * 43758.5453);

    // Only show star if random value is above threshold (controls density)
    if (hash.x > 0.98) {
        vec3 localPos = fract(starCoord);
        vec3 starPos = hash * 0.6 + 0.2; // Star position within cell

        float dist = length(localPos - starPos);
        float brightness = hash.y; // Random brightness

        // Create small, bright points
        float star = smoothstep(0.015, 0.005, dist) * brightness;
        return star;
    }

    return 0.0;
}

vec3 stars(vec2 fragUV) {
  float x = fragUV.x - 0.5 + camera.yaw;
  float y = fragUV.y;

  float dist = sqrt(x*x + y*y);
  float spacing = 0.2;
  float line_width = 0.05 * max(1, audio.smoothed_bass);
  float speed = 0.1;

  // Correctly animate the distance before creating the pattern
  vec3 booster = vec3(audio.smoothed_bass, audio.smoothed_bass, audio.smoothed_bass);
  float animated_dist = dist - mod(camera.time * speed, spacing);
  float pattern = fract(animated_dist / spacing);

  float circle = smoothstep(0.5 - line_width, 0.5, pattern) - smoothstep(0.5, 0.5 + line_width, pattern);

  // Rainbow color calculation
  float hue = mod(camera.time * 0.1, 1.0); // Slower speed for color transition
  vec3 hsv = vec3(hue, 1.0, 1.0); // Full saturation and brightness
  vec3 rgb = hsv2rgb(hsv);

  return (rgb * circle) * booster;
}

vec3 sky(vec2 fragUV) {
  vec2 moon_center = vec2(0.75 - camera.yaw, 0.85);
  float moon_radius = 0.08;
  vec2 inner_moon_center = vec2(0.7 - camera.yaw, 0.8);
  float inner_moon_radius = 0.07;

  float dist_from_moon = distance(fragUV, moon_center);
  float dist_from_inner = distance(fragUV, inner_moon_center);
 
  float hue = mod(camera.time * 0.1, 1.0);
  vec3 extra = hsv2rgb(vec3(hue, 1.0, 1.0));

  // Sky color (background)
  float inv = 0.00025 / (pow(2, dist_from_moon));
  vec3 sky_color = vec3(inv*1.2, inv, inv*1.5) * audio.smoothed_bass + extra * 0.1;

  // Moon color
  float intensity = 0.85 - 5*pow(dist_from_moon, 2);
  vec3 moon_color = vec3(intensity + 0.15, intensity, intensity + 0.25) + extra;

  // Fuzzy moon shape (crescent)
  float outer_disk = 1.0 - smoothstep(moon_radius - 0.01, moon_radius + 0.01, dist_from_moon);
  float inner_hole = smoothstep(inner_moon_radius - 0.01, inner_moon_radius + 0.01, dist_from_inner);
  float moon_shape = outer_disk * inner_hole;

  // Blend moon and sky
  vec3 brightness = mix(sky_color, moon_color, moon_shape);

  return brightness + stars(fragUV);
}

void main()
{
    vec2 uv = fragUV * 2.0 - 1.0;
    vec2 iResolution = vec2(1024.0, 1024.0);
    float screenRatio = iResolution.x / iResolution.y;
    uv.x *= screenRatio;

    float lightEastWestPos = -0.5 * u_light_e_w;
    vec3 ligthPosition = vec3(lightEastWestPos, 0.5, 0.0);

    vec3 lightDirection = normalize(ligthPosition);
    vec3 lightColor = toLinear(vec3(0.99, 0.84, 0.43));

    // Use camera from uniform buffer
    vec3 camPosition = vec3(camera.pos_x, camera.pos_y, camera.pos_z);
    mat3 viewMatrix = computeViewMatrix(camera.yaw, 0.);

    vec3 rayOrigin = camPosition;
    vec3 rayDirection = normalize(viewMatrix * vec3(uv.xy, 1.0));

    float seed = fragUV.x + fragUV.y * iResolution.x;
    vec3 intPos;
    vec2 rayCollision = rayMarching(rayOrigin, rayDirection, 0.1, u_max_distance, intPos, seed);
    float intersectionDistance = rayCollision.x;

    float normalizedStepCost = rayCollision.y / float(u_max_steps);
    float normalizedDistance = intersectionDistance / u_max_distance;

    float terrainHeight = intPos.y / 2.0;
    terrainHeight = smoothstep(0.7, 0.78, terrainHeight);

    vec3 finalColor = vec3(0.0);

    vec3 skyColor = vec3(0.0);

    // Add stars to sky
    // float stars = generateStars(rayDirection);
    // finalColor = skyColor + vec3(stars);
    finalColor = sky(fragUV);

    if (intersectionDistance < u_max_distance && rayCollision.y > 0.)
    {
        vec3 rayTerrainIntersection = rayOrigin + rayDirection * intersectionDistance;
        vec3 terrainNormal = getNormal(rayTerrainIntersection, intersectionDistance, rayOrigin);
        vec3 viewDirection = normalize(rayOrigin - rayTerrainIntersection);

        // Calculate distance from camera for HSV color
        float distanceFromCamera = length(rayTerrainIntersection - camPosition);

        float t = clamp(distanceFromCamera / color_config.max_color_distance, 0.0, 1.0);

        // Interpolate hue based on gradient stops
        float hue = 0.0;
        int num_stops = int(color_config.num_stops);

        for (int i = 0; i < num_stops - 1; i++) {
            float pos1 = color_config.stops[i].x;
            float pos2 = color_config.stops[i + 1].x;

            if (t >= pos1 && t <= pos2) {
                float hue1 = color_config.stops[i].y;
                float hue2 = color_config.stops[i + 1].y;
                float segment_t = (t - pos1) / (pos2 - pos1);
                hue = mix(hue1, hue2, segment_t);
                break;
            }
        }

        // Create HSV color using config values
        vec3 hsvColor = vec3(hue, color_config.saturation, color_config.brightness);
        vec3 albedo = toLinear(hsv2rgb(hsvColor));

        vec3 terrainShading = computeShading(albedo, lightColor, terrainNormal, lightDirection, viewDirection, skyColor, terrainHeight);

        normalizedDistance = mix(0.0, pow(normalizedDistance, 0.9), u_fog);
        finalColor = mix(terrainShading, skyColor, normalizedDistance);
    }

    finalColor = tosRGB(finalColor);
    fragColor = vec4(finalColor, 1.0);
}
