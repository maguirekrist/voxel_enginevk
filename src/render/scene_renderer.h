#pragma once
#include <scenes/scene.h>

class SceneRenderer {
public:

    void init();


    void render_scene(VkCommandBuffer cmd, uint32_t swapchainImageIndex);

    Scene* get_current_scene() const;

    void cleanup();
private:
    static void run_compute(VkCommandBuffer cmd, const std::shared_ptr<Material>& computeMaterial);
    static void draw_fullscreen(VkCommandBuffer cmd, const std::shared_ptr<Material>& presentMaterial);

    void draw_objects(VkCommandBuffer cmd, const std::vector<std::unique_ptr<RenderObject>>& chunks);
    void draw_object(VkCommandBuffer cmd, const RenderObject& object);
    
	std::unordered_map<std::string, std::unique_ptr<Scene>> _scenes;
    Scene* _currentScene = nullptr;
    RenderQueue _renderQueue;

    std::string m_lastMaterialKey;
    Mesh* m_lastMesh = nullptr;
};