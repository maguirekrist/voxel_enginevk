#pragma once

#include "camera.h"
#include "scene.h"
#include "render/resource.h"
#include "game/cube_engine.h"
#include "world/chunk_cache.h"

class GameScene final : public Scene {
public:
    GameObject* _player;

    GameScene();
    ~GameScene() override;
    void update_buffers() override;

    void update(float deltaTime) override;
    void handle_input(const SDL_Event& event) override;
    void handle_keystate(const Uint8* state) override;
    void draw_imgui() override;
    void draw_debug_map();
    void draw_debug_cache();
private:
    void update_fog_ubo() const;
    void update_uniform_buffer() const;

	std::shared_ptr<Resource> _fogResource;
    std::shared_ptr<Resource> _cameraUboResource;

    CubeEngine _game;
    Camera* _camera;

    std::optional<RaycastResult> _targetBlock;

    std::vector<std::unique_ptr<GameObject>> _gameObjects;

    void create_player();
    void create_camera();
};
