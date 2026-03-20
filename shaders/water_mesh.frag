#version 450

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inWorldPosition;
layout(location = 3) in vec2 inLighting;

//output write
layout (location = 0) out vec4 outFragColor;

layout(set = 1, binding = 0) uniform LightingUBO {
    vec4 skyZenithColor;
    vec4 skyHorizonColor;
    vec4 groundColor;
    vec4 sunColor;
    vec4 moonColor;
    vec4 shadowColor;
    vec4 waterShallowColor;
    vec4 waterDeepColor;
    vec4 params1;
    vec4 params2;
    vec4 params3;
} lightingUbo;

layout(set = 2, binding = 0) uniform FogUBO {
    vec3 fogColor;
    vec3 fopEndColor;
    
    vec3 fogCenter;
    float fogRadius;
    ivec2 screenSize;
    mat4 inverseViewProject;
} fogUbo;


// float camNear = 1.0f;
// float camFar = 10000.0f;

float computeFogFactor(float depthValue, float minDist, float maxDist) {
    float fogFactor = clamp((depthValue - minDist) / (maxDist - minDist), 0.0, 1.0);
    return fogFactor;
}

float linearizeDepth(float depth, float near, float far) {
    return near * far / (far - depth * (far - near));
}

void main()
{
    float skylight = clamp(inLighting.x, 0.0, 1.0);
    float waterFogStrength = lightingUbo.params2.w;
    float shadowStrength = lightingUbo.params3.x;
    float curvedSky = pow(skylight, max(0.05, shadowStrength));
    float softSky = curvedSky * curvedSky * (3.0 - (2.0 * curvedSky));
    float hemi = clamp(inNormal.y * 0.5 + 0.5, 0.0, 1.0);
    float depthFactor = clamp((62.0 - inWorldPosition.y) / 24.0, 0.0, 1.0);

    vec3 waterTint = mix(lightingUbo.waterShallowColor.rgb, lightingUbo.waterDeepColor.rgb, depthFactor);
    vec3 skyTint = mix(lightingUbo.skyHorizonColor.rgb, lightingUbo.skyZenithColor.rgb, hemi);
    vec3 litWater = inColor * waterTint * mix(lightingUbo.shadowColor.rgb, lightingUbo.sunColor.rgb, softSky) * mix(vec3(0.85), skyTint, 0.35);

    vec2 ndcPos = (gl_FragCoord.xy / fogUbo.screenSize) * 2.0 - 1.0;
    float ndcDepth = gl_FragCoord.z;

    vec4 clipSpacePos = vec4(ndcPos, ndcDepth, 1.0);

    vec4 worldPos = fogUbo.inverseViewProject * clipSpacePos;
    worldPos /= worldPos.w;

    float distanceToCenter = length(worldPos.xyz - fogUbo.fogCenter);

    //float delta = distanceToCenter - fogUbo.fogRadius;

    float fogDistance = mix(180.0f, 28.0f, clamp(waterFogStrength, 0.0, 1.0));
    float fogFactor = computeFogFactor(distanceToCenter, fogUbo.fogRadius, fogUbo.fogRadius + fogDistance);
    fogFactor = clamp(fogFactor * mix(0.4f, 1.8f, clamp(waterFogStrength, 0.0, 1.0)), 0.0, 1.0);




    // float fragDepth = linearizeDepth(ndcDepth, camNear, camFar);

    // float fogFactor = computeFogFactor(fragDepth, fogUbo.fogStart, fogUbo.fogEnd);

    vec3 foggedColor = mix(litWater.rgb, fogUbo.fogColor, fogFactor);
    float alpha = mix(0.38f, 1.0f, fogFactor);

    outFragColor = vec4(foggedColor.rgb, alpha);
}
