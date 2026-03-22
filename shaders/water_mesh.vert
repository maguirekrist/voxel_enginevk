#version 450
layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vLighting;
layout (location = 4) in vec3 vLocalLight;

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec3 outWorldPosition;
layout (location = 3) out vec2 outLighting;
layout (location = 4) out vec3 outLocalLight;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 projection;
    mat4 view;
    mat4 viewproject;
} ubo;


layout ( push_constant ) uniform constants
{
    mat4 modelMatrix;
    vec4 sampledLocalLightAndSunlight;
    vec4 sampledDynamicLightAndMode;
} PushConstants;


void main() {

    float adjustedYPos = vPosition.y;
    if (vNormal.y > 0.9) {
        adjustedYPos = vPosition.y - 0.10f;
    }

    vec3 worldPosition = vec3(PushConstants.modelMatrix * vec4(vPosition.x, adjustedYPos, vPosition.z, 1.0f));
	gl_Position = ubo.viewproject * vec4(worldPosition, 1.0f);
	outColor = vColor;
    outNormal = normalize(mat3(PushConstants.modelMatrix) * vNormal);
    outWorldPosition = worldPosition;
    outLighting = vLighting;
    outLocalLight = vLocalLight;
}
