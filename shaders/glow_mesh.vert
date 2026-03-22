#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vLighting;
layout (location = 4) in vec3 vLocalLight;

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec2 outQuadUv;
layout (location = 2) out float outIntensity;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 projection;
    mat4 view;
    mat4 viewproject;
} ubo;

layout ( push_constant ) uniform constants
{
    mat4 modelMatrix;
} PushConstants;

void main()
{
    vec3 center = vec3(PushConstants.modelMatrix * vec4(vPosition, 1.0f));
    vec3 cameraRight = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
    vec3 cameraUp = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);
    vec2 corner = vNormal.xy;
    float radius = vLighting.x;
    vec3 worldPosition = center + cameraRight * corner.x * radius + cameraUp * corner.y * radius;

    gl_Position = ubo.viewproject * vec4(worldPosition, 1.0f);
    outColor = vColor;
    outQuadUv = corner;
    outIntensity = vLighting.y;
}
