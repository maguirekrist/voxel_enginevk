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

struct ChunkBufferData {
    ivec2 position;
};

layout ( push_constant ) uniform constants
{
    ivec2 translate;
} PushConstants;


void main() {

    float adjustedYPos = vPosition.y;
    if (vNormal.y > 0.9) {
        adjustedYPos = vPosition.y - 0.10f;
    }

    vec3 worldPosition = vec3(vPosition.x + PushConstants.translate.x, adjustedYPos, vPosition.z + PushConstants.translate.y);
	gl_Position = ubo.viewproject * vec4(worldPosition, 1.0f);
	outColor = vColor;
}