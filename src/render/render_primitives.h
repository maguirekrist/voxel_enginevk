#pragma once

#include <vk_mesh.h>
#include <collections/spare_set.h>

enum class RenderLayer {
    Opaque,
    Transparent
};

struct RenderObject {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    glm::ivec2 xzPos;
    RenderLayer layer;
    dev_collections::sparse_set<RenderObject>::Handle handle;
};
