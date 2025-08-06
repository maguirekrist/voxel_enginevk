#pragma once

#include "camera.h"
#include "cube_engine.h"
#include "scene.h"
#include "render/resource.h"

class GameScene final : public Scene {
public:
    GameScene();
    ~GameScene() override;
    void queue_objects() override;

    void update(float deltaTime) override;
    void handle_input(const SDL_Event& event) override;
    void handle_keystate(const Uint8* state) override;
    void draw_imgui() override;
    void cleanup() override;
private:
    void update_fog_ubo() const;
    void update_uniform_buffer() const;

	std::shared_ptr<Resource> _fogResource;
    std::shared_ptr<Resource> _cameraUboResource;

    CubeEngine _game;
    Camera _camera;

    std::optional<RaycastResult> _targetBlock;
};
