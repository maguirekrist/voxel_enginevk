#pragma once

#include "scene.h"

class BlueprintBuilderScene : public Scene {
public:
    void render(VkCommandBuffer commandBuffer) override;
};

