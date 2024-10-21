#version 450

layout(location = 0) in vec3 inColor;

//output write
layout (location = 0) out vec4 outFragColor;

layout(set = 1, binding = 0) uniform FogUBO {
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
    vec2 ndcPos = (gl_FragCoord.xy / fogUbo.screenSize) * 2.0 - 1.0;
    float ndcDepth = gl_FragCoord.z;

    vec4 clipSpacePos = vec4(ndcPos, ndcDepth, 1.0);

    vec4 worldPos = fogUbo.inverseViewProject * clipSpacePos;
    worldPos /= worldPos.w;

    float distanceToCenter = length(worldPos.xyz - fogUbo.fogCenter);

    //float delta = distanceToCenter - fogUbo.fogRadius;

    float fogFactor = computeFogFactor(distanceToCenter, fogUbo.fogRadius, fogUbo.fogRadius + 60.0f);




    // float fragDepth = linearizeDepth(ndcDepth, camNear, camFar);

    // float fogFactor = computeFogFactor(fragDepth, fogUbo.fogStart, fogUbo.fogEnd);

    vec3 foggedColor = mix(inColor.rgb, fogUbo.fogColor, fogFactor);
    float alpha = mix(0.5f, 1.0f, fogFactor);

    outFragColor = vec4(foggedColor.rgb, alpha);
}