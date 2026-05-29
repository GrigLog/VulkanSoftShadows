#version 460
#extension GL_EXT_ray_query : require

layout(set = 0, binding = 0) uniform SceneUbo {
    mat4 viewProjection;
    vec4 sunPosition;
    mat4 model[16];
    vec4 color[16];
} sceneUbo;

layout(set = 0, binding = 1) uniform accelerationStructureEXT sceneTLAS;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inWorldPosition;
layout(location = 0) out vec4 outFragmentColor;


const int NUM_SHADOW_SAMPLES = 32; // amount of ray queries per pixel
const float COEF_MIN_SHADOW = 0.25; // how dark are the shadows (0.0 is pitch black)
const float SUN_RADIUS = 0.2;


vec2 vogelDiskSample(int idxSample, int numSamples, float angleInitial) {
    // Vogel spiral distributes samples evenly on a unit disk
    const float goldenAngle = 2.39996323;
    float fi = float(idxSample);
    float r = sqrt((fi + 0.5) / float(numSamples)); // +0.5 avoids center sample
    float a = fi * goldenAngle + angleInitial;
    return vec2(cos(a), sin(a)) * r;
}

float hash(vec2 p) {
    // Returns number in [0, 1]
    // Cheap deterministic hash used to rotate the sample pattern per fragment.
    // Without this randomization, the amount of ray queries to get rid of the "banding" artifact is too high (> 100).
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// See https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html for reference
void main() {
    vec3 posSun = sceneUbo.sunPosition.xyz;
    vec3 offSun = posSun - inWorldPosition;
    float distSun = length(offSun);
    vec3 dirSun = distSun > 0.0 ? offSun / distSun : vec3(0.0, 1.0, 0.0);

    // Build an orthonormal basis around the sun direction for disk sampling
    vec3 dirUp = vec3(0.0, 1.0, 0.0);
    vec3 dirTangent = normalize(cross(dirUp, dirSun));
    vec3 dirBitangent = cross(dirSun, dirTangent);

    // Random per-fragment rotation. Fixes "banding" from deterministic sampling
    float angleSampleDisk = hash(inWorldPosition.xz) * 2 * 3.14159; // there is no built-in PI???

    float numVisibleSamples = 0.0;
    // Trace a shadow ray to each disk sample on the area light.
    for (int i = 0; i < NUM_SHADOW_SAMPLES; ++i) {
        vec2 off2DOrthogonal = vogelDiskSample(i, NUM_SHADOW_SAMPLES, angleSampleDisk);
        vec3 posSample =
            posSun +
            dirTangent * (off2DOrthogonal.x * SUN_RADIUS) +
            dirBitangent * (off2DOrthogonal.y * SUN_RADIUS);

        vec3 offSample = posSample - inWorldPosition;
        float distSample = length(offSample);
        vec3 dirSample = offSample / distSample;

        //https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html#VK_KHR_ray_query
        rayQueryEXT shadowQuery;
        rayQueryInitializeEXT(shadowQuery,
                              sceneTLAS, //accStruct
                              gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
                              0xFF, //cullMask
                              inWorldPosition, //origin
                              0.001, //tMin
                              dirSample, //direction
                              distSample //tMax
        );
        rayQueryProceedEXT(shadowQuery);

        bool occluded = rayQueryGetIntersectionTypeEXT(shadowQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT;
        if (!occluded)
            numVisibleSamples += 1.0;
    }

    // Clamp to a minimum shadow term so fully occluded pixels are not pure black.
    float coefVisibility = numVisibleSamples / float(NUM_SHADOW_SAMPLES);
    float coefColor = COEF_MIN_SHADOW + coefVisibility * (1.0 - COEF_MIN_SHADOW);

    outFragmentColor = vec4(inColor.rgb * coefColor, inColor.a);
}
