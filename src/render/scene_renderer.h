#pragma once
#include <scenes/scene.h>

class SceneRenderer {
public:

    void init();


    void render_scene(VkCommandBuffer cmd, uint32_t swapchainImageIndex);

    Scene* get_current_scene();

    void cleanup();
private:
    void run_compute(VkCommandBuffer cmd, const Material& computeMaterial);
    void draw_fullscreen(VkCommandBuffer cmd, Material* presentMaterial);

    void draw_objects(VkCommandBuffer cmd, const std::vector<std::shared_ptr<RenderObject>>& chunks);
	void draw_object(VkCommandBuffer cmd, const RenderObject& object, Mesh* lastMesh, Material* lastMaterial);
    
	std::unordered_map<std::string, std::unique_ptr<Scene>> _scenes;
    Scene* _currentScene = nullptr;
    RenderQueue _renderQueue;
};