#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "camera.h"
#include "config/json_document_store.h"
#include "editing/document_command_history.h"
#include "render/mesh.h"
#include "render/resource.h"
#include "render/scene_render_state.h"
#include "scene.h"
#include "scene_services.h"
#include "ui/ui_runtime.h"
#include "voxel/voxel_assembly_asset.h"
#include "voxel/voxel_assembly_repository.h"
#include "voxel/voxel_asset_manager.h"
#include "voxel/voxel_model_repository.h"
#include "voxel/voxel_render_registry.h"

class VoxelAssemblyScene final : public Scene
{
public:
    explicit VoxelAssemblyScene(const SceneServices& services);
    ~VoxelAssemblyScene() override;

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
    void build_pipelines() override;
    void rebuild_pipelines() override;
    SceneRenderState& get_render_state() override;
    [[nodiscard]] bool wants_mouse_capture() const override { return false; }

private:
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
    void sync_orbit_from_view_matrix(const glm::mat4& viewMatrix);
    void sync_preview_instances();
    void sync_selection_overlay();
    void clear_preview();
    void release_selection_meshes();
    void add_part_from_selected_model();
    void remove_selected_part();
    void mark_preview_dirty();
    [[nodiscard]] glm::vec3 orbit_target() const;
    [[nodiscard]] std::string make_unique_part_id(std::string_view baseId) const;
    [[nodiscard]] VoxelAssemblyPartDefinition* selected_part();
    [[nodiscard]] const VoxelAssemblyPartDefinition* selected_part() const;
    [[nodiscard]] VoxelAssemblySlotDefinition* selected_slot();
    [[nodiscard]] const VoxelAssemblySlotDefinition* selected_slot() const;
    [[nodiscard]] VoxelAssemblyBindingState* selected_binding_state(VoxelAssemblyPartDefinition& part);
    [[nodiscard]] const VoxelAssemblyBindingState* selected_binding_state(const VoxelAssemblyPartDefinition& part) const;
    [[nodiscard]] const VoxelAssemblyBindingState* preview_binding_state(const VoxelAssemblyPartDefinition& part) const;
    VoxelAssemblyBindingState& add_binding_state(VoxelAssemblyPartDefinition& part);
    void ensure_binding_state_selection(const VoxelAssemblyPartDefinition& part);
    void nudge_selected_binding_position(const glm::vec3& delta);
    void rotate_selected_binding_euler_degrees(const glm::vec3& deltaDegrees);
    [[nodiscard]] std::vector<std::string> collect_validation_messages();
    void reset_assembly();
    void save_assembly();
    void load_assembly();
    void load_assembly(const std::string& assetId);
    void refresh_saved_assets();
    void draw_editor_window();
    void apply_assembly_edit(std::string_view description, const std::function<void(VoxelAssemblyAsset&)>& edit);
    void undo_assembly_edit();
    void redo_assembly_edit();
    void sync_selection_indices();

    SceneServices _services;
    SceneRenderState _renderState;
    std::shared_ptr<Resource> _cameraUboResource;
    std::shared_ptr<Resource> _lightingResource;
    std::shared_ptr<Resource> _fogResource;
    std::shared_ptr<Mesh> _assemblyBoundsMesh;
    std::shared_ptr<Mesh> _collisionBoundsMesh;
    std::shared_ptr<Mesh> _assemblyRootPivotMesh;
    std::shared_ptr<Mesh> _selectedPartBoundsMesh;
    std::shared_ptr<Mesh> _selectedPartPivotMesh;
    std::shared_ptr<Mesh> _parentAttachmentMarkerMesh;
    std::vector<std::shared_ptr<Mesh>> _selectedAttachmentMarkerMeshes{};
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _selectedPartBoundsHandle;
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _selectedPartPivotHandle;
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _assemblyBoundsHandle;
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _collisionBoundsHandle;
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _assemblyRootPivotHandle;
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _parentAttachmentMarkerHandle;
    std::vector<dev_collections::sparse_set<RenderObject>::Handle> _selectedAttachmentMarkerHandles{};
    config::JsonFileDocumentStore _documentStore;
    VoxelModelRepository _modelRepository;
    VoxelAssemblyRepository _assemblyRepository;
    VoxelAssetManager _assetManager;
    VoxelRenderRegistry _previewRenderRegistry;
    std::unordered_map<std::string, VoxelRenderInstance> _resolvedPreviewInstances{};
    VoxelAssemblyAsset _assembly;
    editing::DocumentCommandHistory<VoxelAssemblyAsset> _history{50};
    std::unique_ptr<Camera> _camera;
    std::vector<std::string> _savedModelAssetIds{};
    std::vector<std::string> _savedAssemblyAssetIds{};
    int _selectedSavedModelIndex{-1};
    int _selectedSavedAssemblyIndex{-1};
    int _selectedSlotIndex{-1};
    int _selectedPartIndex{-1};
    int _selectedBindingStateIndex{0};
    bool _orbitDragging{false};
    bool _previewDirty{true};
    bool _selectionOverlayDirty{true};
    bool _showAssemblyBounds{true};
    bool _showCollisionBounds{true};
    bool _showAssemblyRootPivot{true};
    bool _showSelectedPartBounds{true};
    bool _showSelectedPartPivot{true};
    bool _showSelectedPartAttachments{true};
    bool _showParentAttachmentMarker{true};
    float _orbitYawDegrees{40.0f};
    float _orbitPitchDegrees{24.0f};
    float _orbitDistance{5.5f};
    glm::vec3 _previewOrbitTarget{0.0f};
    std::string _statusMessage{"Ready"};
    ui::Runtime _runtimeUi{};
};
