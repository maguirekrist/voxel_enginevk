#version 450

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 inQuadUv;
layout (location = 2) in float inIntensity;

layout (location = 0) out vec4 outFragColor;

void main()
{
    float dist = clamp(length(inQuadUv), 0.0, 1.41421356);
    float radial = smoothstep(1.10, 0.15, dist);
    float core = smoothstep(0.55, 0.0, dist);
    float halo = radial * radial;
    float intensity = clamp((halo * 0.65 + core * 1.35) * inIntensity, 0.0, 1.0);
    float alpha = intensity;

    vec3 color = inColor * intensity;
    outFragColor = vec4(color, alpha);
}
