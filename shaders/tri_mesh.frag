#version 450

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inWorldPosition;
layout(location = 3) in vec2 inLighting;

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
} lighting;

//output write
layout (location = 0) out vec4 outFragColor;

void main()
{
    float skylight = clamp(inLighting.x, 0.0, 1.0);
    float ao = clamp(inLighting.y, 0.0, 1.0);
    float dayFactor = lighting.params1.z;
    float aoStrength = lighting.params1.w;
    float shadowFloor = lighting.params2.x;
    float hemiStrength = lighting.params2.y;
    float skylightStrength = lighting.params2.z;
    float shadowStrength = lighting.params3.x;

    float curvedSky = pow(skylight, max(0.05, shadowStrength));
    float softSky = curvedSky * curvedSky * (3.0 - (2.0 * curvedSky));
    float ambient = mix(shadowFloor, 1.0, clamp(softSky * skylightStrength, 0.0, 1.0));

    float hemi = clamp(inNormal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemiTint = mix(lighting.groundColor.rgb, mix(lighting.skyHorizonColor.rgb, lighting.skyZenithColor.rgb, hemi), hemiStrength);
    vec3 lightTint = mix(lighting.shadowColor.rgb, lighting.sunColor.rgb, clamp(softSky * max(dayFactor, 0.2), 0.0, 1.0));
    float contactShadow = mix(1.0, ao, aoStrength);

    vec3 color = inColor * ambient * hemiTint * lightTint * contactShadow;
    outFragColor = vec4(clamp(color, 0.0, 1.0), 1.0f);
}
