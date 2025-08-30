#pragma once

#include "camera.h"
#include "scene.h"
#include "render/resource.h"
#include "game/cube_engine.h"


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
	void rebuild_pipelines() override;
    void build_pipelines() override;

    void draw_debug_map();
    void draw_debug_cache();
private:
    struct CameraUBO {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 viewproject;
    };

    struct FogUBO {
        glm::vec3 fogColor;  // 12 bytes
        float padding1[1];   // 12 bytes of padding to align the next vec3

        glm::vec3 fogEndColor;
        float padding;

        glm::vec3 fogCenter; // 12 bytes
        float fogRadius;     // 4 bytes, occupies the same 16-byte slot as fogCenter

        glm::ivec2 screenSize; // 8 bytes
        float padding2[2];     // 8 bytes of padding to align the next mat4

        glm::mat4 invViewProject; // 64 bytes
    };

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
