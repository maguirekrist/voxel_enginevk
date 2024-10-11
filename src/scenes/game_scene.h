#pragma once

#include "scene.h"

class GameScene : public Scene {
public:
    void render(VkCommandBuffer cmd) override;

private:
    void run_compute(VkCommandBuffer cmd, const Material& computeMaterial, VkDescriptorSet* descriptorSets, size_t setCount);
    void draw_fullscreen(VkCommandBuffer cmd, Material* presentMaterial);

    void draw_objects(VkCommandBuffer cmd, const std::vector<RenderObject>& chunks, bool isTransparent);

	void draw_object(VkCommandBuffer cmd, const RenderObject& object, Mesh* lastMesh, Material* lastMaterial, bool isTransparent);

};