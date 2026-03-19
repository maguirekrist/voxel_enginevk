#pragma once

#include "vk_types.h"

class MaterialManager;
class MeshManager;

struct SceneServices
{
    VmaAllocator allocator{};
    VkExtent2D* windowExtent{};
    MeshManager* meshManager{};
    MaterialManager* materialManager{};

    [[nodiscard]] VkExtent2D current_window_extent() const
    {
        return *windowExtent;
    }
};
