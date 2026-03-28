#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "camera.h"
#include "components/voxel_animation_component.h"
#include "components/voxel_assembly_component.h"
#include "config/json_document_store.h"
#include "editing/document_command_history.h"
#include "render/resource.h"
#include "render/scene_render_state.h"
#include "scene.h"
#include "scene_services.h"
#include "voxel/voxel_animation_asset.h"
#include "voxel/voxel_animation_clip_asset_manager.h"
#include "voxel/voxel_animation_clip_repository.h"
#include "voxel/voxel_animation_controller_asset_manager.h"
#include "voxel/voxel_animation_controller_repository.h"
#include "voxel/voxel_assembly_asset_manager.h"
#include "voxel/voxel_assembly_repository.h"
#include "voxel/voxel_asset_manager.h"
#include "voxel/voxel_model_repository.h"
#include "voxel/voxel_render_registry.h"

class AnimationEditorScene final : public Scene
{
public:
    explicit AnimationEditorScene(const SceneServices& services);
    ~AnimationEditorScene() override;

    void update_buffers() override;
    void update(float deltaTime) override;
    void handle_input(const SDL_Event& event) override;
    void handle_keystate(const Uint8* state) override;
    void clear_input() override;
    void draw_imgui() override;
    void build_pipelines() override;
    void rebuild_pipelines() override;
    SceneRenderState& get_render_state() override;
    [[nodiscard]] bool wants_mouse_capture() const override { return false; }

private:
    enum class EditorMode
    {
        Clip,
        Controller
    };

    struct CameraUBO
    {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 viewproject;
    };

    struct LightingUBO
    {
        static constexpr size_t MaxDynamicLights = 8;

        glm::vec4 skyZenithColor;
        glm::vec4 skyHorizonColor;
        glm::vec4 groundColor;
        glm::vec4 sunColor;
        glm::vec4 moonColor;
        glm::vec4 shadowColor;
        glm::vec4 waterShallowColor;
        glm::vec4 waterDeepColor;
        glm::vec4 params1;
        glm::vec4 params2;
        glm::vec4 params3;
        glm::vec4 params4;
        std::array<glm::vec4, MaxDynamicLights> dynamicLightPositionRadius{};
        std::array<glm::vec4, MaxDynamicLights> dynamicLightColorIntensity{};
        std::array<glm::uvec4, MaxDynamicLights> dynamicLightMetadata{};
    };

    struct FogUBO
    {
        glm::vec3 fogColor;
        float padding1[1];
        glm::vec3 fogEndColor;
        float padding2;
        glm::vec3 fogCenter;
        float fogRadius;
        glm::ivec2 screenSize;
        float padding3[2];
        glm::mat4 invViewProject;
    };

    void update_uniform_buffers() const;
    void update_camera();
    void draw_orientation_gizmo();
    void sync_preview();
    void refresh_asset_lists();
    void pick_part(int mouseX, int mouseY);
    void new_clip();
    void save_clip();
    void load_clip(const std::string& assetId);
    void new_controller();
    void save_controller();
    void load_controller(const std::string& assetId);
    void save_controller_preview_if_dirty();
    void apply_clip_edit(std::string_view description, const std::function<void(VoxelAnimationClipAsset&)>& edit);
    void apply_controller_edit(std::string_view description, const std::function<void(VoxelAnimationControllerAsset&)>& edit);
    void draw_editor_window();
    [[nodiscard]] glm::vec3 orbit_target() const;

    SceneServices _services;
    SceneRenderState _renderState{};
    std::shared_ptr<Resource> _cameraUboResource;
    std::shared_ptr<Resource> _lightingResource;
    std::shared_ptr<Resource> _fogResource;
    config::JsonFileDocumentStore _documentStore{};
    VoxelModelRepository _modelRepository;
    VoxelAssemblyRepository _assemblyRepository;
    VoxelAnimationClipRepository _clipRepository;
    VoxelAnimationControllerRepository _controllerRepository;
    VoxelAssetManager _assetManager;
    VoxelAssemblyAssetManager _assemblyAssetManager;
    VoxelAnimationClipAssetManager _clipAssetManager;
    VoxelAnimationControllerAssetManager _controllerAssetManager;
    VoxelRenderRegistry _previewRegistry{};
    std::unordered_map<std::string, VoxelRenderInstance> _previewParts{};
    editing::DocumentCommandHistory<VoxelAnimationClipAsset> _clipHistory{50};
    editing::DocumentCommandHistory<VoxelAnimationControllerAsset> _controllerHistory{50};
    std::unique_ptr<Camera> _camera;
    VoxelAssemblyComponent _previewAssembly{};
    VoxelAnimationComponent _previewAnimation{};
    VoxelAnimationClipAsset _clip{};
    VoxelAnimationControllerAsset _controller{};
    std::vector<std::string> _assemblyAssetIds{};
    std::vector<std::string> _clipAssetIds{};
    std::vector<std::string> _controllerAssetIds{};
    std::string _selectedAssemblyId{};
    std::string _selectedPartId{};
    std::string _statusMessage{"Ready"};
    EditorMode _mode{EditorMode::Clip};
    float _previewTimeSeconds{0.0f};
    bool _playing{true};
    bool _orbitDragging{false};
    bool _previewDirty{true};
    bool _controllerPreviewDirty{true};
    float _orbitYawDegrees{40.0f};
    float _orbitPitchDegrees{24.0f};
    float _orbitDistance{5.5f};
    glm::vec3 _previewOrbitTarget{0.0f};
    float _previewMoveX{0.0f};
    float _previewMoveY{1.0f};
    float _previewSpeed{1.0f};
    bool _previewGrounded{true};
    float _previewVerticalSpeed{0.0f};
    bool _previewFlyMode{false};
};
