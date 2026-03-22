#pragma once

#include <vk_types.h>
#include <collections/spare_set.h>

struct Mesh;
struct Material;

enum class RenderLayer {
    Opaque,
    Transparent
};

struct ObjectPushConstants {
    glm::mat4 modelMatrix{1.0f};
};

struct RenderObject {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    glm::mat4 transform{1.0f};
    RenderLayer layer;
    //dev_collections::sparse_set<RenderObject>::Handle handle;
};

struct PushConstant {
    VkShaderStageFlags stageFlags;
    uint32_t size;
    std::function<ObjectPushConstants(const RenderObject&)> build_constant;
};
