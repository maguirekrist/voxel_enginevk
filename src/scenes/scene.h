#pragma once

#include "SDL_events.h"
#include <vulkan/vulkan.h>
#include <vk_types.h>

class Scene {
public:
    virtual void init() = 0;
    virtual void render(VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex) = 0;
    virtual void update(float deltaTime) = 0;
    virtual void handle_input(const SDL_Event& event) = 0;
    virtual void handle_keystate(const Uint8* state) = 0;
    virtual void cleanup() = 0;

    virtual ~Scene() = default;
};