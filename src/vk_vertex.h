#pragma once
#include <vk_types.h>

struct VertexInputDescription {

    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;

    VkPipelineVertexInputStateCreateFlags flags = 0;
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


// template<typename T>
// class SharedResource {
// private:
//     std::shared_ptr<T> resource; // The underlying resource
//
// public:
//     SharedResource(std::shared_ptr<T> initialResource)
//         : resource(initialResource) {}
//
//     SharedResource(T&& initialResource) : resource(std::make_shared<T>(initialResource)) {}
//
//     // Get the current resource
//     std::shared_ptr<T> get() const {
//         return resource;
//     }
//
//     // Set a new resource, replacing the old one
//     std::shared_ptr<T> update(std::shared_ptr<T> newResource) {
//         std::shared_ptr<T> oldResource = resource;
//         resource = newResource;
//         return oldResource;
//     }
// };

// template <typename T>
// struct UniformBuffer {
//     T _ubo;
//     AllocatedBuffer _uniformBuffer;
// };
