#pragma once
#include <render/frame_render_context.h>
#include <scenes/scene.h>
#include <scenes/scene_services.h>
#include <render/scene_render_state.h>
#include <vk_types.h>
#include <render/render_primitives.h>

class MeshAllocator;

class SceneRenderer {
public:

    void init(const SceneServices& sceneServices);
    void cleanup();

    void render_scene(VkCommandBuffer cmd, const FrameRenderContext& frameContext);

    std::shared_ptr<Scene> get_current_scene() const;
private:
    static void run_compute(VkCommandBuffer cmd, const FrameRenderContext& frameContext, const std::shared_ptr<Material>& computeMaterial);
    static void draw_fullscreen(VkCommandBuffer cmd, const std::shared_ptr<Material>& presentMaterial);

    void draw_objects(VkCommandBuffer cmd, const std::vector<RenderObject>& chunks);
    void draw_object(VkCommandBuffer cmd, const RenderObject& object);
    
	std::unordered_map<std::string, std::shared_ptr<Scene>> _scenes;
    std::shared_ptr<Scene> _currentScene = nullptr;

    MeshAllocator* m_last_allocator = nullptr;

    std::string m_lastMaterialKey;
};
