#version 450

layout(set = 0, binding = 0) uniform sampler2D atlasTexture;

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

float median3(vec3 sampleValue) {
    return max(min(sampleValue.r, sampleValue.g), min(max(sampleValue.r, sampleValue.g), sampleValue.b));
}

float screen_px_range() {
    vec2 unitRange = vec2(2.0) / vec2(textureSize(atlasTexture, 0));
    vec2 screenTexSize = vec2(1.0) / max(fwidth(fragUv), vec2(1e-4));
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    vec4 atlasSample = texture(atlasTexture, fragUv);
    float signedDistance = median3(atlasSample.rgb);
    float screenPxDistance = screen_px_range() * (signedDistance - 0.5);
    float alpha = clamp(screenPxDistance + 0.5, 0.0, 1.0);

    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
