#pragma once

#include <array>

#include "vk_types.h"

class MaterialManager;

struct FrameRenderContext
{
    VkExtent2D windowExtent{};

    VkRenderPass offscreenPass{};
    VkFramebuffer offscreenFramebuffer{};
    VkClearValue* offscreenClearValues{};
    uint32_t offscreenClearValueCount{};

    VkRenderPass presentPass{};
    VkFramebuffer presentFramebuffer{};
    VkClearValue* presentClearValues{};
    uint32_t presentClearValueCount{};

    ImageResource fullscreenImage{};
    ImageResource depthImage{};

    MaterialManager* materialManager{};
};
