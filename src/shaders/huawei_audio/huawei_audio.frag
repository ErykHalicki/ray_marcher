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
} camera;

// Audio parameters from CPU
layout(set = 2, binding = 1) readonly buffer AudioParams {
    float bass;
    float mid;
    float high;
} audio;

// Constants
#define PI 3.14159
#define PI2 6.28318
#define HFPI 1.57079
#define EPSILON 1e-10

// Exposed variables
const int u_max_steps = 500;
const float u_max_distance = 20.0;
const float u_fog = 1.0;
const float u_specular = 0.5;
const float u_light_e_w = 1.0;

// Cubic fade (C1 smooth) - more performant
vec2 cubicInterpolation(vec2 t)
{
    return t*t*(3.0 - 2.0*t);
}

// Random hash
vec2 hash2(vec2 p)
{
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
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
float fbm(in vec2 uv)
{
    float value = 0.;
    float amplitude = 1.6;
    float freq = 1.0;

    for (int i = 0; i < 8; i++)
    {
        value += perlinNoise(uv * freq) * amplitude;
        amplitude *= 0.4;
        freq *= 2.0;
    }
    // max = 1.6 + (1.6 * 0.4) + (1.6 * 0.4^2) + (1.6 * 0.4^3) ...
    // max = 2.66491904

    return value;
}

float terrainHeightMap(in vec3 uv, in vec3 camPos)
{
    float height = fbm(uv.xz*0.5);

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
    float closeWeight = smoothstep(7.0, 0.0, distanceFromCamera);
    audioMultiplier += closeWeight * audio.high * 1.5;

    // Mid-range mountains - mid frequencies
    float midWeight = smoothstep(0.0, 7.0, distanceFromCamera) * smoothstep(20.0, 7.0, distanceFromCamera);
    audioMultiplier += midWeight * audio.mid * 1.5;

    // Far mountains - bass
    float farWeight = smoothstep(7.0, 14.0, distanceFromCamera);
    audioMultiplier += farWeight * audio.bass * 1.5;

    // Apply audio modulation to height
    height *= (1.0 + audioMultiplier);

    return height;
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

vec2 rayMarching(in vec3 rayOrigin, in vec3 rayDirection, in float minDistance, in float maxDistance, inout vec3 intPos, inout float seed)
{
    float intersectionDistance = minDistance;
    float finalStepCount = 1.0;
    float MAX_HEIGHT = 4.0;

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
        intersectionDistance += (0.6 + hash1(seed)) * height;
    }

    return vec2(intersectionDistance, finalStepCount);
}

vec3 computeShading(vec3 terrainColor, vec3 lightColor, vec3 normal, vec3 lightDirection, vec3 viewDirection, vec3 skyColor, float terrainHeight)
{
    terrainColor = mix(terrainColor, vec3(1.0), terrainHeight);

    vec3 halfVector = normalize(lightDirection + viewDirection);
    float NdH = max(dot(normal, halfVector), 0.0);
    float NdL = max(dot(normal, lightDirection), 0.0);

    vec3 diffuse = (terrainColor / PI) * NdL;
    vec3 ambient = vec3(normal.y * 0.1) * skyColor;

    float specularIntensity = mix(u_specular * 0.2, u_specular, terrainHeight);
    vec3 specular = lightColor * pow(NdH, 10.0) * specularIntensity * NdL;

    return (diffuse + ambient + specular);
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

    vec3 albedo = toLinear(vec3(0.5, 0.39, 0.18));
    vec3 finalColor = vec3(0.0);

    vec3 skyColor = mix(vec3(0.3098, 0.5608, 0.9137), vec3(0.9961, 0.9725, 0.9059), max(dot(rayDirection, lightDirection) * 0.5 + 0.5, 0.0));
    skyColor = toLinear(skyColor);

    finalColor = skyColor;

    if (intersectionDistance < u_max_distance && rayCollision.y > 0.)
    {
        vec3 rayTerrainIntersection = rayOrigin + rayDirection * intersectionDistance;
        vec3 terrainNormal = getNormal(rayTerrainIntersection, intersectionDistance, rayOrigin);
        vec3 viewDirection = normalize(rayOrigin - rayTerrainIntersection);

        vec3 terrainShading = computeShading(albedo, lightColor, terrainNormal, lightDirection, viewDirection, skyColor, terrainHeight);

        normalizedDistance = mix(0.0, pow(normalizedDistance, 0.9), u_fog);
        finalColor = mix(terrainShading, skyColor, normalizedDistance);
    }

    finalColor = tosRGB(finalColor);
    fragColor = vec4(finalColor, 1.0);
}
