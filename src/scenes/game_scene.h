#pragma once

#include "camera.h"
#include "scene.h"
#include "scene_services.h"
#include "render/chunk_render_registry.h"
#include "render/resource.h"
#include "render/scene_render_state.h"
#include "render/mesh.h"
#include "game/cube_engine.h"


class GameScene final : public Scene {
public:
    explicit GameScene(const SceneServices& services);
    ~GameScene() override;
    void update_buffers() override;

    void update(float deltaTime) override;
    void handle_input(const SDL_Event& event) override;
    void handle_keystate(const Uint8* state) override;
    void draw_imgui() override;
	void rebuild_pipelines() override;
    void build_pipelines() override;
    SceneRenderState& get_render_state() override;

    void draw_debug_map();
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
    std::shared_ptr<Mesh> _chunkBoundaryMesh;
    std::shared_ptr<Mesh> _targetBlockOutlineMesh;
    ChunkRenderRegistry _chunkRenderRegistry;
    SceneRenderState _renderState;
    SceneServices _services;
    PlayerInputState _playerInput{};
    std::vector<dev_collections::sparse_set<RenderObject>::Handle> _chunkBoundaryHandles{};
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _targetBlockOutlineHandle{};
    bool _showChunkBoundaries{false};

    CubeEngine _game;
    std::unique_ptr<Camera> _camera;

    std::optional<RaycastResult> _targetBlock;
    std::optional<glm::ivec3> _outlinedBlockWorldPos;

    void create_camera();
    void sync_camera_to_game(float deltaTime);
    void sync_target_block();
    void sync_target_block_outline();
    void sync_chunk_boundary_debug();
    void clear_target_block_outline();
    void clear_chunk_boundary_debug();
};
