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

template<typename T>
class SharedResource {
private:
    std::shared_ptr<T> resource; // The underlying resource

public:
    SharedResource(std::shared_ptr<T> initialResource)
        : resource(initialResource) {}

    // Get the current resource
    std::shared_ptr<T> get() const {
        return resource;
    }

    // Set a new resource, replacing the old one
    std::shared_ptr<T> update(std::shared_ptr<T> newResource) {
        std::shared_ptr<T> oldResource = resource;
        resource = newResource;
        return oldResource;
    }
};
struct CameraUBO {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 viewproject;
};

struct FogUBO {
    glm::vec3 fogColor;  // 12 bytes
    float padding1[1];   // 12 bytes of padding to align the next vec3

    glm::vec3 fogEndColor;
    float padding;

    glm::vec3 fogCenter; // 12 bytes
    float fogRadius;     // 4 bytes, occupies the same 16-byte slot as fogCenter

    glm::ivec2 screenSize; // 8 bytes
    float padding2[2];     // 8 bytes of padding to align the next mat4

    glm::mat4 invViewProject; // 64 bytes
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
	std::shared_ptr<SharedResource<Mesh>> mesh;
	Material* material;
	glm::ivec2 xzPos;
};