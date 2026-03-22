#pragma once

#include "camera.h"
#include "scene.h"
#include "scene_services.h"
#include "render/chunk_render_registry.h"
#include "render/resource.h"
#include "render/scene_render_state.h"
#include "render/mesh.h"
#include "settings/game_settings.h"
#include "game/cube_engine.h"
#include "world/terrain_gen.h"


class GameScene final : public Scene {
public:
    explicit GameScene(const SceneServices& services);
    ~GameScene() override;
    void update_buffers() override;

    void update(float deltaTime) override;
    void handle_input(const SDL_Event& event) override;
    void handle_keystate(const Uint8* state) override;
    void clear_input() override;
    void draw_imgui() override;
    void rebuild_pipelines() override;
    void build_pipelines() override;
    SceneRenderState& get_render_state() override;
    [[nodiscard]] bool wants_mouse_capture() const override { return true; }

    void draw_debug_map();
private:
    struct CameraUBO {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 viewproject;
    };

    struct LightingUBO {
        glm::vec4 skyZenithColor;
        glm::vec4 skyHorizonColor;
        glm::vec4 groundColor;
        glm::vec4 sunColor;
        glm::vec4 moonColor;
        glm::vec4 shadowColor;
        glm::vec4 waterShallowColor;
        glm::vec4 waterDeepColor;
        glm::vec4 params1; // x=timeOfDay, y=sunHeight, z=dayFactor, w=aoStrength
        glm::vec4 params2; // x=shadowFloor, y=hemiStrength, z=skylightStrength, w=waterFogStrength
        glm::vec4 params3; // x=shadowStrength, y=localLightStrength
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
    void update_lighting_ubo() const;
    void update_uniform_buffer() const;

    std::shared_ptr<Resource> _fogResource;
    std::shared_ptr<Resource> _cameraUboResource;
    std::shared_ptr<Resource> _lightingResource;
    std::shared_ptr<Mesh> _chunkBoundaryMesh;
    std::shared_ptr<Mesh> _targetBlockOutlineMesh;
    ChunkRenderRegistry _chunkRenderRegistry;
    SceneRenderState _renderState;
    SceneServices _services;
    PlayerInputState _playerInput{};
    std::vector<dev_collections::sparse_set<RenderObject>::Handle> _chunkBoundaryHandles{};
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _targetBlockOutlineHandle{};
    settings::SettingsManager _settings{};
    int _viewDistanceDraft{GameConfig::DEFAULT_VIEW_DISTANCE};
    TerrainGeneratorSettings _worldGenDraft{};
    int _worldGenPreviewLayer{0};
    bool _worldGenDraftInitialized{false};

    CubeEngine _game;
    std::unique_ptr<Camera> _camera;

    std::optional<RaycastResult> _targetBlock;
    std::optional<glm::ivec3> _outlinedBlockWorldPos;

    void create_camera();
    void sync_camera_to_game(float deltaTime);
    void sync_target_block();
    void sync_target_block_outline();
    void sync_chunk_boundary_debug();
    void bind_settings();
    void apply_view_distance_settings(const settings::ViewDistanceRuntimeSettings& settings);
    void apply_ambient_occlusion_settings(const settings::AmbientOcclusionRuntimeSettings& settings);
    void sync_world_gen_draft();
    void clear_target_block_outline();
    void clear_chunk_boundary_debug();
};
