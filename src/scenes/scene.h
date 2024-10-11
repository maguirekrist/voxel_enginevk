#pragma once

#include <vulkan/vulkan.h>
#include <vk_types.h>

class Scene {
public:
    virtual void render(VkCommandBuffer commandBuffer) = 0;
};