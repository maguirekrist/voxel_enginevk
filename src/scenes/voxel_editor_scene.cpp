#include "voxel_editor_scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

#include <glm/trigonometric.hpp>

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "render/material_manager.h"
#include "render/mesh_manager.h"
#include "render/mesh_release_queue.h"
#include "voxel/voxel_mesher.h"
#include "vk_util.h"

namespace
{
    constexpr glm::vec3 EditorBackgroundColor{0.06f, 0.07f, 0.09f};
    constexpr glm::vec3 EditorFogColor{0.12f, 0.14f, 0.17f};

    struct CellCoord
    {
        int x{0};
        int y{0};
    };

    CellCoord rotate_forward(const CellCoord coord, const int width, const int height, const int quarterTurns)
    {
        switch (quarterTurns & 3)
        {
        case 1:
            return CellCoord{ height - 1 - coord.y, coord.x };
        case 2:
            return CellCoord{ width - 1 - coord.x, height - 1 - coord.y };
        case 3:
            return CellCoord{ coord.y, width - 1 - coord.x };
        default:
            return coord;
        }
    }

    CellCoord rotate_inverse(const CellCoord coord, const int width, const int height, const int quarterTurns)
    {
        switch (quarterTurns & 3)
        {
        case 1:
            return CellCoord{ coord.y, height - 1 - coord.x };
        case 2:
            return CellCoord{ width - 1 - coord.x, height - 1 - coord.y };
        case 3:
            return CellCoord{ width - 1 - coord.y, coord.x };
        default:
            return coord;
        }
    }

    glm::vec3 orbit_front(const float yawDegrees, const float pitchDegrees)
    {
        const float yaw = glm::radians(yawDegrees);
        const float pitch = glm::radians(pitchDegrees);
        return glm::normalize(glm::vec3(
            std::cos(pitch) * std::cos(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::sin(yaw)));
    }
}

VoxelEditorScene::VoxelEditorScene(const SceneServices& services) :
    _services(services),
    _documentStore(),
    _repository(_documentStore)
{
    const ResourceBackendContext resourceBackend{
        .device = _services.device,
        .allocator = _services.allocator
    };

    const auto cameraBuffer = vkutil::create_buffer(
        _services.allocator,
        sizeof(CameraUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);
    const auto lightingBuffer = vkutil::create_buffer(
        _services.allocator,
        sizeof(LightingUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);
    const auto fogBuffer = vkutil::create_buffer(
        _services.allocator,
        sizeof(FogUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    _cameraUboResource = std::make_shared<Resource>(resourceBackend, Resource::BUFFER, Resource::ResourceValue(cameraBuffer));
    _lightingResource = std::make_shared<Resource>(resourceBackend, Resource::BUFFER, Resource::ResourceValue(lightingBuffer));
    _fogResource = std::make_shared<Resource>(resourceBackend, Resource::BUFFER, Resource::ResourceValue(fogBuffer));

    _camera = std::make_unique<Camera>(glm::vec3(0.0f, 1.5f, -4.0f), _services.current_window_extent());

    reset_model();
    refresh_saved_assets();
    build_pipelines();
    update_camera();
    update_uniform_buffers();
}

VoxelEditorScene::~VoxelEditorScene()
{
    if (_previewHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_previewHandle.value());
        _previewHandle.reset();
    }
    release_preview_mesh();
}

void VoxelEditorScene::update_buffers()
{
    sync_model_mesh();
    update_uniform_buffers();
}

void VoxelEditorScene::update(const float deltaTime)
{
    (void)deltaTime;
    update_camera();
}

void VoxelEditorScene::handle_input(const SDL_Event& event)
{
    if (event.type == SDL_MOUSEWHEEL)
    {
        _orbitDistance = std::clamp(_orbitDistance - (static_cast<float>(event.wheel.y) * 0.2f), 0.5f, 64.0f);
    }
}

void VoxelEditorScene::handle_keystate(const Uint8* state)
{
    (void)state;
}

void VoxelEditorScene::clear_input()
{
}

void VoxelEditorScene::draw_imgui()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    draw_editor_window();
    ImGui::Render();
}

void VoxelEditorScene::build_pipelines()
{
    const auto translate = PushConstant{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = sizeof(ObjectPushConstants),
        .build_constant = [](const RenderObject& object) -> ObjectPushConstants
        {
            ObjectPushConstants push{};
            push.chunk_translate = object.xzPos;
            return push;
        }
    };

    _services.materialManager->build_graphics_pipeline(
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource)
        },
        { translate },
        {},
        "tri_mesh.vert.spv",
        "tri_mesh.frag.spv",
        "defaultmesh");

    _services.materialManager->build_postprocess_pipeline(_fogResource);
    _services.materialManager->build_present_pipeline();
}

void VoxelEditorScene::rebuild_pipelines()
{
    _camera->resize(_services.current_window_extent());
    build_pipelines();
}

SceneRenderState& VoxelEditorScene::get_render_state()
{
    return _renderState;
}

void VoxelEditorScene::update_uniform_buffers() const
{
    CameraUBO cameraUbo{};
    cameraUbo.projection = _camera->_projection;
    cameraUbo.view = _camera->_view;
    cameraUbo.viewproject = _camera->_projection * _camera->_view;

    void* cameraData = nullptr;
    vmaMapMemory(_services.allocator, _cameraUboResource->value.buffer._allocation, &cameraData);
    memcpy(cameraData, &cameraUbo, sizeof(CameraUBO));
    vmaUnmapMemory(_services.allocator, _cameraUboResource->value.buffer._allocation);

    LightingUBO lighting{};
    lighting.skyZenithColor = glm::vec4(0.30f, 0.43f, 0.60f, 1.0f);
    lighting.skyHorizonColor = glm::vec4(0.62f, 0.66f, 0.70f, 1.0f);
    lighting.groundColor = glm::vec4(0.16f, 0.16f, 0.18f, 1.0f);
    lighting.sunColor = glm::vec4(1.0f, 0.96f, 0.86f, 1.0f);
    lighting.moonColor = glm::vec4(0.18f, 0.20f, 0.28f, 1.0f);
    lighting.shadowColor = glm::vec4(0.16f, 0.18f, 0.21f, 1.0f);
    lighting.params1 = glm::vec4(0.32f, 1.0f, 1.0f, 0.0f);
    lighting.params2 = glm::vec4(0.10f, 0.80f, 1.0f, 0.0f);
    lighting.params3 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

    void* lightingData = nullptr;
    vmaMapMemory(_services.allocator, _lightingResource->value.buffer._allocation, &lightingData);
    memcpy(lightingData, &lighting, sizeof(LightingUBO));
    vmaUnmapMemory(_services.allocator, _lightingResource->value.buffer._allocation);

    FogUBO fog{};
    fog.fogColor = EditorFogColor;
    fog.fogEndColor = EditorBackgroundColor;
    fog.fogCenter = orbit_target();
    fog.fogRadius = 256.0f;
    const VkExtent2D extent = _services.current_window_extent();
    fog.screenSize = glm::ivec2(extent.width, extent.height);
    fog.invViewProject = glm::inverse(_camera->_projection * _camera->_view);

    void* fogData = nullptr;
    vmaMapMemory(_services.allocator, _fogResource->value.buffer._allocation, &fogData);
    memcpy(fogData, &fog, sizeof(FogUBO));
    vmaUnmapMemory(_services.allocator, _fogResource->value.buffer._allocation);
}

void VoxelEditorScene::sync_model_mesh()
{
    if (!_meshDirty)
    {
        return;
    }

    _meshDirty = false;

    if (_previewHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_previewHandle.value());
        _previewHandle.reset();
    }

    release_preview_mesh();

    _previewMesh = VoxelMesher::generate_mesh(_model);
    if (_previewMesh == nullptr || _previewMesh->_indices.empty())
    {
        return;
    }

    _services.meshManager->UploadQueue.enqueue(_previewMesh);
    _previewHandle = _renderState.opaqueObjects.insert(RenderObject{
        .mesh = _previewMesh,
        .material = _services.materialManager->get_material("defaultmesh"),
        .xzPos = glm::ivec2(0),
        .layer = RenderLayer::Opaque
    });
}

void VoxelEditorScene::release_preview_mesh()
{
    if (_previewMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_previewMesh));
    }
}

void VoxelEditorScene::update_camera()
{
    _orbitPitchDegrees = std::clamp(_orbitPitchDegrees, -85.0f, 85.0f);
    _orbitDistance = std::clamp(_orbitDistance, 0.5f, 64.0f);

    const glm::vec3 front = orbit_front(_orbitYawDegrees, _orbitPitchDegrees);
    const glm::vec3 target = orbit_target();
    _camera->_front = front;
    _camera->_up = glm::vec3(0.0f, 1.0f, 0.0f);
    _camera->_position = target - (front * _orbitDistance);
    _camera->update(0.0f);
}

void VoxelEditorScene::clamp_slice_index()
{
    int maxSlice = 0;
    switch (_editAxis)
    {
    case EditAxis::X:
        maxSlice = std::max(_gridDimensions.x - 1, 0);
        break;
    case EditAxis::Y:
        maxSlice = std::max(_gridDimensions.y - 1, 0);
        break;
    case EditAxis::Z:
    default:
        maxSlice = std::max(_gridDimensions.z - 1, 0);
        break;
    }

    _sliceIndex = std::clamp(_sliceIndex, 0, maxSlice);
}

VoxelEditorScene::PlaneSpec VoxelEditorScene::plane_spec() const
{
    switch (_editAxis)
    {
    case EditAxis::X:
        return PlaneSpec{
            .width = std::max(_gridDimensions.z, 1),
            .height = std::max(_gridDimensions.y, 1)
        };
    case EditAxis::Y:
        return PlaneSpec{
            .width = std::max(_gridDimensions.x, 1),
            .height = std::max(_gridDimensions.z, 1)
        };
    case EditAxis::Z:
    default:
        return PlaneSpec{
            .width = std::max(_gridDimensions.x, 1),
            .height = std::max(_gridDimensions.y, 1)
        };
    }
}

void VoxelEditorScene::draw_editor_window()
{
    ImGui::Begin("Voxel Editor");

    ImGui::Text("Scene Switch: F1 Game | F2 Voxel Editor");
    ImGui::SeparatorText("Asset");

    char assetIdBuffer[128]{};
    char displayNameBuffer[128]{};
    strncpy_s(assetIdBuffer, _model.assetId.c_str(), _TRUNCATE);
    strncpy_s(displayNameBuffer, _model.displayName.c_str(), _TRUNCATE);

    if (ImGui::InputText("Asset Id", assetIdBuffer, IM_ARRAYSIZE(assetIdBuffer)))
    {
        _model.assetId = assetIdBuffer;
    }
    if (ImGui::InputText("Display Name", displayNameBuffer, IM_ARRAYSIZE(displayNameBuffer)))
    {
        _model.displayName = displayNameBuffer;
    }
    if (ImGui::InputFloat("Voxel Size", &_model.voxelSize, 0.0f, 0.0f, "%.4f"))
    {
        _model.voxelSize = std::max(_model.voxelSize, 0.001f);
        mark_model_dirty();
    }

    ImGui::Text("Save Path: %s", _repository.resolve_path(_model.assetId).string().c_str());

    if (ImGui::Button("New Model"))
    {
        reset_model();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        load_model();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        save_model();
    }

    ImGui::SeparatorText("Saved Assets");
    if (ImGui::Button("Refresh List"))
    {
        refresh_saved_assets();
    }
    ImGui::SameLine();
    ImGui::Text("%d file(s)", static_cast<int>(_savedAssetIds.size()));

    const float assetListHeight = std::min(220.0f, 28.0f + (static_cast<float>(_savedAssetIds.size()) * ImGui::GetTextLineHeightWithSpacing()));
    if (ImGui::BeginChild("SavedVoxelAssets", ImVec2(0.0f, assetListHeight), true))
    {
        if (_savedAssetIds.empty())
        {
            ImGui::TextDisabled("No saved .vxm assets found in %s", _repository.root_path().string().c_str());
        }
        else
        {
            for (int index = 0; index < static_cast<int>(_savedAssetIds.size()); ++index)
            {
                const bool selected = index == _selectedSavedAssetIndex;
                if (ImGui::Selectable(_savedAssetIds[index].c_str(), selected))
                {
                    _selectedSavedAssetIndex = index;
                    _model.assetId = _savedAssetIds[index];
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    load_model(_savedAssetIds[index]);
                }
            }
        }
    }
    ImGui::EndChild();

    if (_selectedSavedAssetIndex < 0 || _selectedSavedAssetIndex >= static_cast<int>(_savedAssetIds.size()))
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Load Selected"))
    {
        load_model(_savedAssetIds[_selectedSavedAssetIndex]);
    }
    if (_selectedSavedAssetIndex < 0 || _selectedSavedAssetIndex >= static_cast<int>(_savedAssetIds.size()))
    {
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Editing");
    int axis = static_cast<int>(_editAxis);
    const char* axisLabels[] = { "Slice X", "Slice Y", "Slice Z" };
    if (ImGui::Combo("Edit Plane", &axis, axisLabels, IM_ARRAYSIZE(axisLabels)))
    {
        _editAxis = static_cast<EditAxis>(axis);
        clamp_slice_index();
    }

    if (ImGui::InputInt3("Grid Dimensions", &_gridDimensions.x))
    {
        _gridDimensions.x = std::max(_gridDimensions.x, 1);
        _gridDimensions.y = std::max(_gridDimensions.y, 1);
        _gridDimensions.z = std::max(_gridDimensions.z, 1);
        clamp_slice_index();
    }

    clamp_slice_index();
    int maxSlice = 0;
    switch (_editAxis)
    {
    case EditAxis::X:
        maxSlice = std::max(_gridDimensions.x - 1, 0);
        break;
    case EditAxis::Y:
        maxSlice = std::max(_gridDimensions.y - 1, 0);
        break;
    case EditAxis::Z:
    default:
        maxSlice = std::max(_gridDimensions.z - 1, 0);
        break;
    }
    ImGui::SliderInt("Slice Index", &_sliceIndex, 0, maxSlice);
    ImGui::SliderInt("Grid Rotation", &_rotationQuarterTurns, 0, 3);
    ImGui::Checkbox("Erase Mode", &_eraseMode);

    float paintColor[4]{
        static_cast<float>(_paintColor.r) / 255.0f,
        static_cast<float>(_paintColor.g) / 255.0f,
        static_cast<float>(_paintColor.b) / 255.0f,
        static_cast<float>(_paintColor.a) / 255.0f
    };
    if (ImGui::ColorEdit4("Paint Color", paintColor))
    {
        _paintColor = VoxelColor{
            .r = static_cast<uint8_t>(std::round(std::clamp(paintColor[0], 0.0f, 1.0f) * 255.0f)),
            .g = static_cast<uint8_t>(std::round(std::clamp(paintColor[1], 0.0f, 1.0f) * 255.0f)),
            .b = static_cast<uint8_t>(std::round(std::clamp(paintColor[2], 0.0f, 1.0f) * 255.0f)),
            .a = static_cast<uint8_t>(std::round(std::clamp(paintColor[3], 0.0f, 1.0f) * 255.0f))
        };
    }

    ImGui::SeparatorText("Preview Camera");
    ImGui::SliderFloat("Yaw", &_orbitYawDegrees, -180.0f, 180.0f, "%.1f deg");
    ImGui::SliderFloat("Pitch", &_orbitPitchDegrees, -80.0f, 80.0f, "%.1f deg");
    ImGui::SliderFloat("Distance", &_orbitDistance, 0.5f, 48.0f, "%.2f");

    const VoxelBounds bounds = _model.bounds();
    const glm::ivec3 dimensions = bounds.dimensions();
    ImGui::SeparatorText("Model Stats");
    ImGui::Text("Voxel Count: %llu", static_cast<unsigned long long>(_model.voxel_count()));
    ImGui::Text("Bounds: %d x %d x %d", dimensions.x, dimensions.y, dimensions.z);
    ImGui::TextWrapped("%s", _statusMessage.c_str());

    ImGui::SeparatorText("Slice Grid");
    const PlaneSpec plane = plane_spec();
    const bool rotatedOdd = (_rotationQuarterTurns & 1) != 0;
    const int displayWidth = rotatedOdd ? plane.height : plane.width;
    const int displayHeight = rotatedOdd ? plane.width : plane.height;
    const float maxCanvasWidth = ImGui::GetContentRegionAvail().x;
    const float cellSize = std::max(10.0f, std::min(maxCanvasWidth / static_cast<float>(std::max(displayWidth, 1)), 32.0f));
    const ImVec2 canvasSize(
        cellSize * static_cast<float>(displayWidth),
        cellSize * static_cast<float>(displayHeight));
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
    ImDrawList* const drawList = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("VoxelSliceCanvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(16, 18, 24, 255), 4.0f);

    for (int logicalY = 0; logicalY < plane.height; ++logicalY)
    {
        for (int logicalX = 0; logicalX < plane.width; ++logicalX)
        {
            const CellCoord display = rotate_forward(CellCoord{ logicalX, logicalY }, plane.width, plane.height, _rotationQuarterTurns);
            const int screenRow = (displayHeight - 1) - display.y;
            const ImVec2 cellMin(
                canvasMin.x + (static_cast<float>(display.x) * cellSize),
                canvasMin.y + (static_cast<float>(screenRow) * cellSize));
            const ImVec2 cellMax(cellMin.x + cellSize, cellMin.y + cellSize);

            VoxelCoord voxelCoord{};
            switch (_editAxis)
            {
            case EditAxis::X:
                voxelCoord = VoxelCoord{ _sliceIndex, logicalY, logicalX };
                break;
            case EditAxis::Y:
                voxelCoord = VoxelCoord{ logicalX, _sliceIndex, logicalY };
                break;
            case EditAxis::Z:
            default:
                voxelCoord = VoxelCoord{ logicalX, logicalY, _sliceIndex };
                break;
            }

            if (const VoxelColor* color = _model.try_get(voxelCoord); color != nullptr)
            {
                drawList->AddRectFilled(
                    cellMin,
                    cellMax,
                    IM_COL32(color->r, color->g, color->b, color->a));
            }

            drawList->AddRect(cellMin, cellMax, IM_COL32(70, 78, 92, 255));
        }
    }

    if (ImGui::IsItemHovered())
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int displayX = std::clamp(static_cast<int>((mouse.x - canvasMin.x) / cellSize), 0, displayWidth - 1);
        const int displayRow = std::clamp(static_cast<int>((mouse.y - canvasMin.y) / cellSize), 0, displayHeight - 1);
        const int displayY = (displayHeight - 1) - displayRow;
        const CellCoord logical = rotate_inverse(CellCoord{ displayX, displayY }, plane.width, plane.height, _rotationQuarterTurns);

        VoxelCoord voxelCoord{};
        switch (_editAxis)
        {
        case EditAxis::X:
            voxelCoord = VoxelCoord{ _sliceIndex, logical.y, logical.x };
            break;
        case EditAxis::Y:
            voxelCoord = VoxelCoord{ logical.x, _sliceIndex, logical.y };
            break;
        case EditAxis::Z:
        default:
            voxelCoord = VoxelCoord{ logical.x, logical.y, _sliceIndex };
            break;
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (_eraseMode)
            {
                if (_model.remove_voxel(voxelCoord))
                {
                    mark_model_dirty();
                }
            }
            else
            {
                _model.set_voxel(voxelCoord, _paintColor);
                mark_model_dirty();
            }
        }
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            if (_model.remove_voxel(voxelCoord))
            {
                mark_model_dirty();
            }
        }

        ImGui::BeginTooltip();
        ImGui::Text("Voxel: %d, %d, %d", voxelCoord.x, voxelCoord.y, voxelCoord.z);
        ImGui::Text("LMB: Paint  RMB: Erase");
        ImGui::EndTooltip();
    }

    ImGui::End();
}

void VoxelEditorScene::mark_model_dirty()
{
    _meshDirty = true;
}

void VoxelEditorScene::reset_model()
{
    _model = VoxelModel{};
    _model.assetId = "untitled";
    _model.displayName = "Untitled";
    _model.voxelSize = 1.0f / 16.0f;
    _model.set_voxel(VoxelCoord{ 0, 0, 0 }, _paintColor);
    _gridDimensions = glm::ivec3(16, 16, 16);
    _editAxis = EditAxis::Z;
    _sliceIndex = 0;
    _rotationQuarterTurns = 0;
    _statusMessage = "Created a new voxel model.";
    mark_model_dirty();
}

void VoxelEditorScene::save_model()
{
    try
    {
        _repository.save(_model);
        refresh_saved_assets();
        _statusMessage = std::format("Saved {} voxel(s) to {}", _model.voxel_count(), _repository.resolve_path(_model.assetId).string());
    }
    catch (const std::exception& ex)
    {
        _statusMessage = std::format("Save failed: {}", ex.what());
    }
}

void VoxelEditorScene::load_model()
{
    load_model(_model.assetId);
}

void VoxelEditorScene::load_model(const std::string& assetId)
{
    if (const auto loaded = _repository.load(assetId); loaded.has_value())
    {
        _model = loaded.value();
        _statusMessage = std::format("Loaded {}", _repository.resolve_path(_model.assetId).string());
        refresh_saved_assets();
        mark_model_dirty();
        return;
    }

    _statusMessage = std::format("No voxel model found at {}", _repository.resolve_path(assetId).string());
}

void VoxelEditorScene::refresh_saved_assets()
{
    _savedAssetIds = _repository.list_asset_ids();
    _selectedSavedAssetIndex = -1;

    for (int index = 0; index < static_cast<int>(_savedAssetIds.size()); ++index)
    {
        if (_savedAssetIds[index] == _model.assetId)
        {
            _selectedSavedAssetIndex = index;
            break;
        }
    }
}

glm::vec3 VoxelEditorScene::orbit_target() const
{
    const VoxelBounds bounds = _model.bounds();
    if (!bounds.valid)
    {
        return glm::vec3(0.0f);
    }

    return (bounds.center() * _model.voxelSize) - _model.pivot;
}
