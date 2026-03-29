#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec4 fragColor;

layout(push_constant) uniform UiPushConstants {
    vec2 viewportSize;
    vec2 padding;
} pushConstants;

void main() {
    vec2 ndc = vec2(
        (inPosition.x / pushConstants.viewportSize.x) * 2.0 - 1.0,
        (inPosition.y / pushConstants.viewportSize.y) * 2.0 - 1.0);

    fragUv = inUv;
    fragColor = inColor;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
