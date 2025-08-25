#pragma once

#include <vk_types.h>
#include <collections/spare_set.h>
#include "mesh_allocator.h"

struct MeshPayload;
struct Material;

enum class RenderLayer {
    Opaque,
    Transparent
};

struct ObjectPushConstants {
    glm::ivec2 chunk_translate;
};

struct RenderObject {
    std::shared_ptr<MeshRef> mesh;
    std::shared_ptr<Material> material;
    glm::ivec2 xzPos;
    RenderLayer layer;

    //dev_collections::sparse_set<RenderObject>::Handle handle;

    ~RenderObject();
};

struct PushConstant {
    VkShaderStageFlags stageFlags;
    uint32_t size;
    std::function<ObjectPushConstants(const RenderObject&)> build_constant;
};
