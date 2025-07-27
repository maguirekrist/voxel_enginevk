#pragma once

#include "camera.h"
#include "cube_engine.h"
#include "scene.h"
#include "vk_mesh.h"
#include "render/resource.h"

class GameScene final : public Scene {
public:
    GameScene();
    void render(RenderQueue& queue) override;
    
    void init() override;
    void update(const float deltaTime) override;
    void handle_input(const SDL_Event& event) override;
    void handle_keystate(const Uint8* state) override;
    void cleanup() override;
private:
    void update_fog_ubo();
    void update_uniform_buffer();
    
	std::shared_ptr<Resource> _fogResource;
    std::shared_ptr<Resource> _cameraUboResource;

    CubeEngine _game;
    Camera _camera;

    std::vector<std::shared_ptr<RenderObject>> _gameObjects;

    std::optional<RaycastResult> _targetBlock;
};
