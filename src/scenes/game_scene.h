#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include "camera.h"
#include "scene.h"
#include "scene_services.h"
#include "render/chunk_render_registry.h"
#include "render/chunk_decoration_render_registry.h"
#include "render/resource.h"
#include "render/scene_render_state.h"
#include "render/mesh.h"
#include "settings/game_settings.h"
#include "game/cube_engine.h"
#include "physics/aabb.h"
#include "world/terrain_gen.h"
#include "world/dynamic_light_registry.h"
#include "world/world_light_sampler.h"
#include "ui/ui_runtime.h"
#include "voxel/voxel_component_render_adapter.h"
#include "voxel/voxel_render_registry.h"


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
    void build_runtime_ui(ui::FrameBuilder& builder) override;
    void submit_ui_signal(const ui::Signal& signal) override;
    void collect_world_labels(ui::WorldLabelCollector& collector) override;
    [[nodiscard]] ui::Runtime& runtime_ui() override;
    void rebuild_pipelines() override;
    void build_pipelines() override;
    SceneRenderState& get_render_state() override;
    [[nodiscard]] bool wants_mouse_capture() const override { return true; }

    void draw_debug_map();
private:
    struct SpatialColliderDebugMeshCacheEntry
    {
        std::shared_ptr<Mesh> mesh{};
        AABB localBounds{
            .min = glm::vec3(0.0f),
            .max = glm::vec3(0.0f)
        };
        bool boundsCached{false};
    };

    struct CameraUBO {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 viewproject;
    };

    struct LightingUBO {
        static constexpr size_t MaxDynamicLights = 8;

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
        glm::vec4 params3; // x=shadowStrength, y=localLightStrength, z=dynamicLightCount
        glm::vec4 params4; // x=dynamicLightStrength
        std::array<glm::vec4, MaxDynamicLights> dynamicLightPositionRadius{};
        std::array<glm::vec4, MaxDynamicLights> dynamicLightColorIntensity{};
        std::array<glm::uvec4, MaxDynamicLights> dynamicLightMetadata{};
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
    void draw_camera_orientation_gizmo() const;

    std::shared_ptr<Resource> _fogResource;
    std::shared_ptr<Resource> _cameraUboResource;
    std::shared_ptr<Resource> _lightingResource;
    std::shared_ptr<Mesh> _chunkBoundaryMesh;
    std::shared_ptr<Mesh> _targetBlockOutlineMesh;
    ChunkRenderRegistry _chunkRenderRegistry;
    ChunkDecorationRenderRegistry _chunkDecorationRenderRegistry;
    VoxelRenderRegistry _playerVoxelRenderRegistry;
    VoxelRenderRegistry _voxelRenderRegistry;
    world_lighting::DynamicLightRegistry _dynamicLightRegistry;
    SceneRenderState _renderState;
    SceneServices _services;
    PlayerInputState _playerInput{};
    std::vector<dev_collections::sparse_set<RenderObject>::Handle> _chunkBoundaryHandles{};
    std::vector<dev_collections::sparse_set<RenderObject>::Handle> _spatialColliderDebugHandles{};
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
    std::string _runtimeVoxelAssetId{"flower"};
    std::string _runtimeVoxelStatus{"Voxel prop demo not loaded yet."};
    bool _runtimeVoxelDemoInitialized{false};
    bool _runtimeVoxelDemoDirty{true};
    bool _playerTorchLightEnabled{true};
    float _playerTorchRadius{7.5f};
    float _playerTorchIntensity{1.0f};
    glm::vec3 _playerTorchColor{1.0f, 0.82f, 0.52f};
    int _runtimeUiFontPresetIndex{0};
    float _runtimeUiSamplePixelHeight{26.0f};
    std::array<char, 160> _runtimeUiSampleText{
        "Baseline sample: H A k g y i l O Q p q j 0123456789"
    };
    std::optional<world_lighting::DynamicLightRegistry::LightId> _playerTorchLightId{};
    std::unique_ptr<world_lighting::WorldLightSampler> _worldLightSampler;
    ui::Runtime _runtimeUi{};
    std::unordered_map<std::string, VoxelRenderRegistry::InstanceId> _playerAssemblyInstanceIds{};
    std::vector<std::string> _savedPlayerAssemblyIds{};
    int _selectedPlayerAssemblyIndex{-1};
    std::string _playerAssemblyAssetId{};
    std::string _playerAssemblyStatus{"No player assembly selected."};
    bool _showSpatialColliderBounds{false};
    std::unordered_map<std::string, SpatialColliderDebugMeshCacheEntry> _spatialColliderDebugMeshCache{};

    void create_camera();
    void sync_camera_to_game(float deltaTime);
    void sync_player_render_instance();
    void sync_spatial_collider_debug();
    void refresh_player_assembly_assets();
    void sync_runtime_lights();
    void sync_target_block();
    void sync_target_block_outline();
    void sync_chunk_boundary_debug();
    void rebuild_runtime_voxel_demo();
    [[nodiscard]] glm::vec3 runtime_voxel_demo_position(const glm::ivec2& offset) const;
    void bind_settings();
    void apply_view_distance_settings(const settings::ViewDistanceRuntimeSettings& settings);
    void apply_ambient_occlusion_settings(const settings::AmbientOcclusionRuntimeSettings& settings);
    void apply_player_settings(const settings::PlayerRuntimeSettings& settings);
    void sync_world_gen_draft();
    [[nodiscard]] ui::FontFamilyId runtime_ui_font_family() const;
    void clear_target_block_outline();
    void clear_spatial_collider_debug();
    void release_spatial_collider_debug_meshes();
    void clear_chunk_boundary_debug();
};
