#version 450

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 inQuadUv;
layout (location = 2) in float inIntensity;

layout (location = 0) out vec4 outFragColor;

void main()
{
    float dist = clamp(length(inQuadUv), 0.0, 1.41421356);
    float radial = smoothstep(1.05, 0.0, dist);
    float core = smoothstep(0.45, 0.0, dist);
    float alpha = clamp((radial * 0.7 + core * 0.5) * inIntensity, 0.0, 1.0);

    vec3 color = inColor * (radial * 0.9 + core * 0.8);
    outFragColor = vec4(color, alpha);
}
