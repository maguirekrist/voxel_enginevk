#pragma once

#include <memory>
#include <vk_types.h>

struct VertexInputDescription {

    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;

    VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct MeshPushConstants {
    glm::ivec2 translation;
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;

    static VertexInputDescription get_vertex_description();
};

struct PointVertex {
    glm::vec3 position;

    static VertexInputDescription get_vertex_description();
};

struct Mesh {
    std::vector<Vertex> _vertices;
    AllocatedBuffer _vertexBuffer;
    std::vector<uint32_t> _indices;
    AllocatedBuffer _indexBuffer;
    bool _isActive{false};

    static Mesh create_cube_mesh();
};

struct CameraUBO {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 viewproject;
};

struct ChunkPushConstants {
    glm::ivec2 chunk_translate;
};

struct ChunkBufferObject {
    glm::ivec2 chunkPosition;
};

template <typename T>
struct UniformBuffer {
    T _ubo;
    AllocatedBuffer _uniformBuffer;
};

struct RenderObject {
	std::shared_ptr<Mesh> mesh;
	Material* material;
	glm::ivec2 xzPos;
};