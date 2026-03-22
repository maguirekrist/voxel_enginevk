#version 450

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inWorldPosition;
layout(location = 3) in vec2 inLighting;
layout(location = 4) in vec3 inLocalLight;
layout(location = 5) in vec4 inSampledLocalLightAndSunlight;
layout(location = 6) in vec4 inSampledDynamicLightAndMode;

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
    vec4 params4;
    vec4 dynamicLightPositionRadius[8];
    vec4 dynamicLightColorIntensity[8];
    uvec4 dynamicLightMetadata[8];
} lighting;

//output write
layout (location = 0) out vec4 outFragColor;

const uint LIGHTING_MODE_BAKED_CHUNK = 0u;
const uint LIGHTING_MODE_SAMPLED_RUNTIME = 1u;
const uint LIGHTING_MODE_BAKED_PLUS_DYNAMIC = 2u;
const uint LIGHTING_MODE_UNLIT = 3u;
const uint AFFECT_WORLD = 1u;

vec3 sample_dynamic_world_lights(vec3 worldPosition, uint affectMask)
{
    vec3 accumulated = vec3(0.0);
    int lightCount = int(clamp(lighting.params3.z, 0.0, 8.0));
    for (int index = 0; index < lightCount; ++index)
    {
        if ((lighting.dynamicLightMetadata[index].x & affectMask) == 0u || lighting.dynamicLightMetadata[index].y == 0u)
        {
            continue;
        }

        vec3 lightPosition = lighting.dynamicLightPositionRadius[index].xyz;
        float radius = lighting.dynamicLightPositionRadius[index].w;
        float intensity = lighting.dynamicLightColorIntensity[index].w;
        if (radius <= 0.0 || intensity <= 0.0)
        {
            continue;
        }

        vec3 toLight = lightPosition - worldPosition;
        float distanceSquared = dot(toLight, toLight);
        float radiusSquared = radius * radius;
        if (distanceSquared >= radiusSquared)
        {
            continue;
        }

        float distance = sqrt(distanceSquared);
        float attenuation = 1.0 - clamp(distance / radius, 0.0, 1.0);
        float falloff = attenuation * attenuation * intensity * lighting.params4.x;
        accumulated += lighting.dynamicLightColorIntensity[index].xyz * falloff;
    }

    return accumulated;
}

void main()
{
    uint lightingMode = uint(round(inSampledDynamicLightAndMode.w));
    float skylight = clamp(inLighting.x, 0.0, 1.0);
    float ao = clamp(inLighting.y, 0.0, 1.0);
    vec3 localLight = inLocalLight;
    vec3 dynamicLight = vec3(0.0);
    if (lightingMode == LIGHTING_MODE_SAMPLED_RUNTIME)
    {
        skylight = clamp(inSampledLocalLightAndSunlight.w, 0.0, 1.0);
        localLight = inSampledLocalLightAndSunlight.xyz;
        dynamicLight = inSampledDynamicLightAndMode.xyz;
    }
    else if (lightingMode == LIGHTING_MODE_BAKED_PLUS_DYNAMIC)
    {
        dynamicLight = sample_dynamic_world_lights(inWorldPosition, AFFECT_WORLD);
    }

    if (lightingMode == LIGHTING_MODE_UNLIT)
    {
        outFragColor = vec4(clamp(inColor, 0.0, 1.0), 1.0);
        return;
    }

    float dayFactor = lighting.params1.z;
    float aoStrength = lighting.params1.w;
    float shadowFloor = lighting.params2.x;
    float hemiStrength = lighting.params2.y;
    float skylightStrength = lighting.params2.z;
    float shadowStrength = lighting.params3.x;
    float localLightStrength = lighting.params3.y;

    float curvedSky = pow(skylight, max(0.05, shadowStrength));
    float softSky = curvedSky * curvedSky * (3.0 - (2.0 * curvedSky));
    float ambient = mix(shadowFloor, 1.0, clamp(softSky * skylightStrength, 0.0, 1.0));

    float hemi = clamp(inNormal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemiTint = mix(lighting.groundColor.rgb, mix(lighting.skyHorizonColor.rgb, lighting.skyZenithColor.rgb, hemi), hemiStrength);
    vec3 lightTint = mix(lighting.shadowColor.rgb, lighting.sunColor.rgb, clamp(softSky * max(dayFactor, 0.2), 0.0, 1.0));
    float contactShadow = mix(1.0, ao, aoStrength);

    vec3 ambientColor = inColor * ambient * hemiTint * lightTint * contactShadow;
    vec3 emissiveColor = inColor * (localLight + dynamicLight) * localLightStrength;
    vec3 color = ambientColor + emissiveColor;
    outFragColor = vec4(clamp(color, 0.0, 1.0), 1.0f);
}
