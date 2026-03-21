#pragma once

#include "vk_types.h"

class MaterialManager;
class MeshManager;
namespace config { class ConfigService; }

struct SceneServices
{
    VmaAllocator allocator{};
    VkDevice device{VK_NULL_HANDLE};
    VkExtent2D* windowExtent{};
    MeshManager* meshManager{};
    MaterialManager* materialManager{};
    config::ConfigService* configService{};

    [[nodiscard]] VkExtent2D current_window_extent() const
    {
        return *windowExtent;
    }
};
