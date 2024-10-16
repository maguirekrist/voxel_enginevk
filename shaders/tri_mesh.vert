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

layout (set = 1, binding = 0) readonly buffer ChunkBufferObject {
    ChunkBufferData chunks[];
} chunkBuffer;

layout ( push_constant ) uniform constants
{
    ivec2 translate;
} PushConstants;


void main()
{
    // ChunkBufferData chunkData = chunkBuffer.chunks[PushConstants.index];
    vec3 worldPosition = vec3(vPosition.x + PushConstants.translate.x, vPosition.y, vPosition.z + PushConstants.translate.y);
	gl_Position = ubo.viewproject * vec4(worldPosition, 1.0f);
	outColor = vColor;
}