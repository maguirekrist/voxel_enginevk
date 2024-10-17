#pragma once

#include "camera.h"
#include "cube_engine.h"
#include "scene.h"
#include "vk_mesh.h"

class GameScene : public Scene {
public:
    void render(VkCommandBuffer cmd, uint32_t swapchainImageIndex) override;
    
    void update(float deltaTime) override;
    void handle_input(const SDL_Event& event) override;
    void handle_keystate(const Uint8* state) override;
    void cleanup() override;
private:
    void run_compute(VkCommandBuffer cmd, const Material& computeMaterial);
    void draw_fullscreen(VkCommandBuffer cmd, Material* presentMaterial);

    void draw_objects(VkCommandBuffer cmd, const std::vector<RenderObject>& chunks);

	void draw_object(VkCommandBuffer cmd, const RenderObject& object, Mesh* lastMesh, Material* lastMaterial);

    void update_fog_ubo();
    void update_chunk_buffer();
    void update_uniform_buffer();
    
    // Shared Resources
    std::array<VkDescriptorSet, 3> _computeDescriptorSets;
	AllocatedBuffer _fogUboBuffer;
	VkDescriptorSet _fogSet;

    CubeEngine _game;
    Camera _camera;

    std::optional<RaycastResult> _targetBlock;
};