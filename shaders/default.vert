#version 450
layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;

layout (location = 0) out vec3 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 projection;
    mat4 view;
    mat4 viewproject;
} ubo;

layout ( push_constant ) uniform constants
{
    mat4 modelMatrix;
} PushConstants;

void main() {
    vec3 worldPosition = vec3(PushConstants.modelMatrix * vec4(vPosition, 1.0f));
	gl_Position = ubo.viewproject * vec4(worldPosition, 1.0f);
	outColor = vColor;
}
