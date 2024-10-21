#pragma once

#include "scene.h"

class BlueprintBuilderScene : public Scene {
public:
    void init() override;
    void render(VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex) override;
    void update(float deltaTime) override;
    void handle_input(const SDL_Event& event) override;
    void handle_keystate(const Uint8* state) override;
    void cleanup() override;
};

