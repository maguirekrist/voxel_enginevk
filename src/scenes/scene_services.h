#pragma once

#include "vk_types.h"

class MaterialManager;
class MeshManager;
namespace vkutil
{
    class DescriptorAllocator;
    class DescriptorLayoutCache;
}
namespace config { class ConfigService; }

struct SceneServices
{
    VmaAllocator allocator{};
    VkDevice device{VK_NULL_HANDLE};
    VkExtent2D* windowExtent{};
    VkRenderPass* presentRenderPass{};
    MeshManager* meshManager{};
    MaterialManager* materialManager{};
    vkutil::DescriptorAllocator* descriptorAllocator{};
    vkutil::DescriptorLayoutCache* descriptorLayoutCache{};
    QueueFamily uploadQueue{};
    config::ConfigService* configService{};

    [[nodiscard]] VkExtent2D current_window_extent() const
    {
        return *windowExtent;
    }
};
