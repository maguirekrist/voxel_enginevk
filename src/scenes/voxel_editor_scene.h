#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/block.h"
#include "camera.h"
#include "config/json_document_store.h"
#include "editing/document_command_history.h"
#include "render/mesh.h"
#include "render/resource.h"
#include "render/scene_render_state.h"
#include "scene.h"
#include "scene_services.h"
#include "voxel/voxel_model.h"
#include "voxel/voxel_model_repository.h"

class VoxelEditorScene final : public Scene
{
public:
    explicit VoxelEditorScene(const SceneServices& services);
    ~VoxelEditorScene() override;

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

    enum class EditAxis : uint8_t
    {
        X = 0,
        Y = 1,
        Z = 2
    };

    struct PlaneSpec
    {
        int width{1};
        int height{1};
    };

    struct HoverTarget
    {
        VoxelCoord voxel{};
        FaceDirection face{FRONT_FACE};
        float distance{0.0f};
    };

    void update_uniform_buffers() const;
    void sync_model_mesh();
    void sync_hover_target();
    void sync_hover_outline();
    void sync_marker_overlays();
    void release_preview_mesh();
    void release_outline_mesh();
    void release_marker_meshes();
    void ensure_attachment_selection();
    [[nodiscard]] VoxelAttachment* selected_attachment();
    [[nodiscard]] const VoxelAttachment* selected_attachment() const;
    [[nodiscard]] std::string make_unique_attachment_name(std::string_view baseName) const;
    void update_camera();
    void draw_orientation_gizmo();
    void sync_orbit_from_view_matrix(const glm::mat4& viewMatrix);
    void clamp_slice_index();
    [[nodiscard]] PlaneSpec plane_spec() const;
    void draw_editor_window();
    void mark_model_dirty();
    void apply_model_edit(std::string_view description, const std::function<void(VoxelModel&)>& edit);
    void undo_model_edit();
    void redo_model_edit();
    void reset_model();
    void save_model();
    void load_model();
    void load_model(const std::string& assetId);
    void refresh_saved_assets();
    bool try_pick_paint_color_from_hovered_voxel();
    [[nodiscard]] bool is_within_grid(const VoxelCoord& coord) const;
    [[nodiscard]] std::optional<HoverTarget> raycast_hover_target() const;
    [[nodiscard]] glm::vec3 orbit_target() const;

    SceneServices _services;
    SceneRenderState _renderState;

    std::shared_ptr<Resource> _cameraUboResource;
    std::shared_ptr<Resource> _lightingResource;
    std::shared_ptr<Resource> _fogResource;
    std::shared_ptr<Mesh> _previewMesh;
    std::shared_ptr<Mesh> _outlineMesh;
    std::shared_ptr<Mesh> _pivotMarkerMesh;
    std::shared_ptr<Mesh> _pivotVoxelMesh;
    std::shared_ptr<Mesh> _selectedAttachmentVoxelMesh;
    std::vector<std::shared_ptr<Mesh>> _attachmentMarkerMeshes{};
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _previewHandle;
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _outlineHandle;
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _pivotMarkerHandle;
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _pivotVoxelHandle;
    std::optional<dev_collections::sparse_set<RenderObject>::Handle> _selectedAttachmentVoxelHandle;
    std::vector<dev_collections::sparse_set<RenderObject>::Handle> _attachmentMarkerHandles{};

    std::unique_ptr<Camera> _camera;

    config::JsonFileDocumentStore _documentStore;
    VoxelModelRepository _repository;
    VoxelModel _model;
    editing::DocumentCommandHistory<VoxelModel> _history{50};
    glm::ivec3 _gridDimensions{16, 16, 16};
    EditAxis _editAxis{EditAxis::Z};
    int _sliceIndex{0};
    int _rotationQuarterTurns{0};
    VoxelColor _paintColor{255, 64, 64, 255};
    bool _eraseMode{false};
    bool _orbitDragging{false};
    bool _meshDirty{true};
    bool _markerDirty{true};
    bool _showPivotMarker{true};
    bool _showPivotVoxel{true};
    bool _showAttachmentMarkers{true};
    bool _showSelectedAttachmentVoxel{true};
    float _orbitYawDegrees{40.0f};
    float _orbitPitchDegrees{24.0f};
    float _orbitDistance{3.5f};
    std::vector<std::string> _savedAssetIds{};
    int _selectedSavedAssetIndex{-1};
    int _selectedAttachmentIndex{-1};
    std::optional<HoverTarget> _hoveredTarget;
    std::optional<VoxelCoord> _outlinedVoxelCoord;
    bool _outlineShowsRemoval{false};
    std::string _statusMessage{"Ready"};
};
