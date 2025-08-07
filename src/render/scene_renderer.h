#pragma once
#include <scenes/scene.h>

class SceneRenderer {
public:

    void init();
    void cleanup();

    void render_scene(VkCommandBuffer cmd, uint32_t swapchainImageIndex);

    std::shared_ptr<Scene> get_current_scene() const;
private:
    static void run_compute(VkCommandBuffer cmd, const std::shared_ptr<Material>& computeMaterial);
    static void draw_fullscreen(VkCommandBuffer cmd, const std::shared_ptr<Material>& presentMaterial);

    void draw_objects(VkCommandBuffer cmd, const std::vector<RenderObject>& chunks);
    void draw_object(VkCommandBuffer cmd, const RenderObject& object);
    
	std::unordered_map<std::string, std::shared_ptr<Scene>> _scenes;
    std::shared_ptr<Scene> _currentScene = nullptr;

    std::string m_lastMaterialKey;
};