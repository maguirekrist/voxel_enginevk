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
    void clear_input() override;
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

    struct LightingTuning
    {
        glm::vec3 daySkyZenith{0.58f, 0.80f, 1.0f};
        glm::vec3 daySkyHorizon{0.96f, 0.97f, 1.0f};
        glm::vec3 dayGround{0.60f, 0.48f, 0.36f};
        glm::vec3 daySun{1.18f, 1.06f, 0.92f};
        glm::vec3 dayShadow{0.78f, 0.86f, 1.0f};
        glm::vec3 dayFog{0.74f, 0.88f, 1.0f};
        glm::vec3 dayWaterShallow{0.42f, 0.82f, 0.95f};
        glm::vec3 dayWaterDeep{0.12f, 0.34f, 0.58f};

        glm::vec3 duskSkyHorizon{1.0f, 0.56f, 0.35f};
        glm::vec3 duskFog{0.93f, 0.56f, 0.42f};

        glm::vec3 nightSkyZenith{0.03f, 0.05f, 0.11f};
        glm::vec3 nightSkyHorizon{0.10f, 0.08f, 0.16f};
        glm::vec3 nightGround{0.10f, 0.08f, 0.11f};
        glm::vec3 nightSun{0.50f, 0.56f, 0.82f};
        glm::vec3 nightMoon{0.34f, 0.42f, 0.62f};
        glm::vec3 nightShadow{0.14f, 0.18f, 0.30f};
        glm::vec3 nightFog{0.06f, 0.10f, 0.18f};
        glm::vec3 nightWaterShallow{0.10f, 0.22f, 0.30f};
        glm::vec3 nightWaterDeep{0.02f, 0.05f, 0.10f};

        float aoStrength{0.10f};
        float shadowFloor{0.84f};
        float hemiStrength{0.50f};
        float skylightStrength{1.0f};
        float shadowStrength{1.0f};
        float localLightStrength{1.1f};
        float waterFogStrength{0.35f};
        float cycleDurationSeconds{180.0f};
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
    bool _showChunkBoundaries{false};
    bool _ambientOcclusionEnabled{false};
    bool _timeOfDayPaused{false};
    int _viewDistanceSetting{GameConfig::DEFAULT_VIEW_DISTANCE};
    float _timeOfDay{0.32f};
    LightingTuning _lightingTuning{};

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
