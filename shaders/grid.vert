#version 450


layout (location = 0) in vec3 vPosition;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 projection;
    mat4 view;
    mat4 viewproject;
} ubo;

void main()
{
    // ChunkBufferData chunkData = chunkBuffer.chunks[PushConstants.index];
    vec3 worldPosition = vec3(vPosition.x, vPosition.y, vPosition.z);
	gl_Position = ubo.viewproject * vec4(worldPosition, 1.0f);
}