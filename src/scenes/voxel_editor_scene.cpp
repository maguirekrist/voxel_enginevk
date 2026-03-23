#include "voxel_editor_scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

#include <glm/trigonometric.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <SDL.h>

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "third_party/imoguizmo/imoguizmo.hpp"
#include "render/material_manager.h"
#include "render/mesh_manager.h"
#include "render/mesh_release_queue.h"
#include "string_utils.h"
#include "voxel/voxel_picking.h"
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
    if (_outlineHandle.has_value())
    {
        _renderState.transparentObjects.remove(_outlineHandle.value());
        _outlineHandle.reset();
    }
    release_preview_mesh();
    release_outline_mesh();
}

void VoxelEditorScene::update_buffers()
{
    sync_model_mesh();
    sync_hover_outline();
    update_uniform_buffers();
}

void VoxelEditorScene::update(const float deltaTime)
{
    (void)deltaTime;
    update_camera();
    sync_hover_target();
}

void VoxelEditorScene::handle_input(const SDL_Event& event)
{
    if (event.type == SDL_MOUSEMOTION)
    {
        if (_orbitDragging)
        {
            _orbitYawDegrees += static_cast<float>(event.motion.xrel) * 0.18f;
            _orbitPitchDegrees -= static_cast<float>(event.motion.yrel) * 0.18f;
        }
        return;
    }

    if (event.type == SDL_MOUSEWHEEL)
    {
        _orbitDistance = std::clamp(_orbitDistance - (static_cast<float>(event.wheel.y) * 0.2f), 0.5f, 64.0f);
        return;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN)
    {
        sync_hover_target();

        if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            _orbitDragging = true;
        }
        else if (event.button.button == SDL_BUTTON_LEFT)
        {
            if (_eraseMode)
            {
                if (_hoveredTarget.has_value() && _model.remove_voxel(_hoveredTarget->voxel))
                {
                    _statusMessage = std::format("Removed voxel {}, {}, {}", _hoveredTarget->voxel.x, _hoveredTarget->voxel.y, _hoveredTarget->voxel.z);
                    mark_model_dirty();
                    sync_hover_target();
                }
            }
            else if (_hoveredTarget.has_value())
            {
                const FaceDirection face = _hoveredTarget->face;
                const VoxelCoord placedCoord{
                    .x = _hoveredTarget->voxel.x + faceOffsetX[face],
                    .y = _hoveredTarget->voxel.y + faceOffsetY[face],
                    .z = _hoveredTarget->voxel.z + faceOffsetZ[face]
                };

                if (is_within_grid(placedCoord))
                {
                    _model.set_voxel(placedCoord, _paintColor);
                    _statusMessage = std::format("Placed voxel {}, {}, {}", placedCoord.x, placedCoord.y, placedCoord.z);
                    mark_model_dirty();
                    sync_hover_target();
                }
            }
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            if (_hoveredTarget.has_value() && _model.remove_voxel(_hoveredTarget->voxel))
            {
                _statusMessage = std::format("Removed voxel {}, {}, {}", _hoveredTarget->voxel.x, _hoveredTarget->voxel.y, _hoveredTarget->voxel.z);
                mark_model_dirty();
                sync_hover_target();
            }
        }
    }
    else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_MIDDLE)
    {
        _orbitDragging = false;
    }
}

void VoxelEditorScene::handle_keystate(const Uint8* state)
{
    (void)state;
}

void VoxelEditorScene::clear_input()
{
    _orbitDragging = false;
}

void VoxelEditorScene::draw_imgui()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    draw_editor_window();
    draw_orientation_gizmo();
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
            push.modelMatrix = object.transform;
            push.sampledLocalLightAndSunlight = glm::vec4(object.sampledLight.localLight, object.sampledLight.sunlight);
            push.sampledDynamicLightAndMode = glm::vec4(object.sampledLight.dynamicLight, static_cast<float>(object.lightingMode));
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

    _services.materialManager->build_graphics_pipeline(
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Alpha, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST },
        "editor_preview.vert.spv",
        "editor_preview.frag.spv",
        "editorpreview");

    _services.materialManager->build_graphics_pipeline(
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Opaque, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
        "tri_mesh.vert.spv",
        "tri_mesh.frag.spv",
        "chunkboundary");

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
    lighting.waterShallowColor = glm::vec4(0.18f, 0.28f, 0.36f, 1.0f);
    lighting.waterDeepColor = glm::vec4(0.08f, 0.14f, 0.20f, 1.0f);
    lighting.params1 = glm::vec4(0.32f, 1.0f, 1.0f, 0.0f);
    lighting.params2 = glm::vec4(0.10f, 0.80f, 1.0f, 0.0f);
    lighting.params3 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    lighting.params4 = glm::vec4(0.0f);

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
        .transform = glm::mat4(1.0f),
        .layer = RenderLayer::Opaque
    });

    sync_hover_outline();
}

void VoxelEditorScene::sync_hover_target()
{
    _hoveredTarget = raycast_hover_target();
}

void VoxelEditorScene::sync_hover_outline()
{
    std::optional<VoxelCoord> outlineCoord{};
    bool showsRemoval = false;

    if (_hoveredTarget.has_value())
    {
        if (_eraseMode)
        {
            outlineCoord = _hoveredTarget->voxel;
            showsRemoval = true;
        }
        else
        {
            const FaceDirection face = _hoveredTarget->face;
            const VoxelCoord candidate{
                .x = _hoveredTarget->voxel.x + faceOffsetX[face],
                .y = _hoveredTarget->voxel.y + faceOffsetY[face],
                .z = _hoveredTarget->voxel.z + faceOffsetZ[face]
            };

            if (is_within_grid(candidate) && !_model.contains(candidate))
            {
                outlineCoord = candidate;
            }
        }
    }

    if (_outlinedVoxelCoord == outlineCoord && _outlineShowsRemoval == showsRemoval && _outlineHandle.has_value())
    {
        return;
    }

    if (_outlineHandle.has_value())
    {
        _renderState.transparentObjects.remove(_outlineHandle.value());
        _outlineHandle.reset();
    }
    release_outline_mesh();

    _outlinedVoxelCoord = outlineCoord;
    _outlineShowsRemoval = showsRemoval;

    if (!outlineCoord.has_value())
    {
        return;
    }

    const glm::vec3 minCorner = (glm::vec3(
        static_cast<float>(outlineCoord->x),
        static_cast<float>(outlineCoord->y),
        static_cast<float>(outlineCoord->z)) * _model.voxelSize) - _model.pivot;
    _outlineMesh = Mesh::create_block_preview_mesh(minCorner, _model.voxelSize);
    _services.meshManager->UploadQueue.enqueue(_outlineMesh);
    _outlineHandle = _renderState.transparentObjects.insert(RenderObject{
        .mesh = _outlineMesh,
        .material = _services.materialManager->get_material("editorpreview"),
        .transform = glm::mat4(1.0f),
        .layer = RenderLayer::Transparent
    });
}

void VoxelEditorScene::release_preview_mesh()
{
    if (_previewMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_previewMesh));
    }
}

void VoxelEditorScene::release_outline_mesh()
{
    if (_outlineMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_outlineMesh));
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

void VoxelEditorScene::draw_orientation_gizmo()
{
    ImGuiViewport* const viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    constexpr float gizmoSize = 110.0f;
    constexpr float gizmoPadding = 16.0f;

    glm::mat4 gizmoView = _camera->_view;
    const glm::mat4 gizmoProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);

    ImOGuizmo::SetRect(
        viewport->WorkPos.x + viewport->WorkSize.x - gizmoSize - gizmoPadding,
        viewport->WorkPos.y + gizmoPadding,
        gizmoSize);
    ImOGuizmo::BeginFrame(false);

    if (ImOGuizmo::DrawGizmo(glm::value_ptr(gizmoView), glm::value_ptr(gizmoProjection), _orbitDistance))
    {
        sync_orbit_from_view_matrix(gizmoView);
    }
}

void VoxelEditorScene::sync_orbit_from_view_matrix(const glm::mat4& viewMatrix)
{
    const glm::mat4 inverseView = glm::inverse(viewMatrix);
    const glm::vec3 target = orbit_target();
    const glm::vec3 position = glm::vec3(inverseView[3]);
    glm::vec3 front = glm::normalize(target - position);

    if (!std::isfinite(front.x) || !std::isfinite(front.y) || !std::isfinite(front.z) || glm::length(front) <= 0.0001f)
    {
        return;
    }

    _orbitDistance = glm::distance(position, target);
    _orbitYawDegrees = glm::degrees(std::atan2(front.z, front.x));
    _orbitPitchDegrees = glm::degrees(std::asin(std::clamp(front.y, -1.0f, 1.0f)));
    _camera->_position = position;
    _camera->_front = front;
    _camera->_up = glm::vec3(0.0f, 1.0f, 0.0f);
    _camera->_view = viewMatrix;
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
    copy_cstr_truncating(assetIdBuffer, _model.assetId);
    copy_cstr_truncating(displayNameBuffer, _model.displayName);

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

    glm::vec3 pivotVoxelUnits = _model.pivot / _model.voxelSize;
    if (ImGui::InputFloat3("Pivot (Voxel Units)", &pivotVoxelUnits.x, "%.3f"))
    {
        _model.pivot = pivotVoxelUnits * _model.voxelSize;
        mark_model_dirty();
    }

    if (ImGui::Button("Pivot To Grid Center XZ"))
    {
        _model.pivot.x = static_cast<float>(_gridDimensions.x) * 0.5f * _model.voxelSize;
        _model.pivot.z = static_cast<float>(_gridDimensions.z) * 0.5f * _model.voxelSize;
        mark_model_dirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Pivot To Bounds Center"))
    {
        _model.pivot = _model.bounds().center() * _model.voxelSize;
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
    ImGui::Text("Editor Controls: MMB drag orbit, wheel zoom, LMB place, RMB remove.");
    ImGui::Text("Placement/removal uses the actual mouse cursor over the 3D view.");
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

    if (_outlineHandle.has_value())
    {
        _renderState.transparentObjects.remove(_outlineHandle.value());
        _outlineHandle.reset();
    }
    release_outline_mesh();
    _outlinedVoxelCoord.reset();
    _outlineShowsRemoval = false;

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
    _hoveredTarget.reset();
    mark_model_dirty();
    sync_hover_target();
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
        const glm::ivec3 bounds = _model.bounds().dimensions();
        _gridDimensions.x = std::max(_gridDimensions.x, std::max(bounds.x, 1));
        _gridDimensions.y = std::max(_gridDimensions.y, std::max(bounds.y, 1));
        _gridDimensions.z = std::max(_gridDimensions.z, std::max(bounds.z, 1));
        _statusMessage = std::format("Loaded {}", _repository.resolve_path(_model.assetId).string());
        refresh_saved_assets();
        sync_hover_target();
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

bool VoxelEditorScene::is_within_grid(const VoxelCoord& coord) const
{
    return coord.x >= 0 && coord.x < _gridDimensions.x &&
        coord.y >= 0 && coord.y < _gridDimensions.y &&
        coord.z >= 0 && coord.z < _gridDimensions.z;
}

std::optional<VoxelEditorScene::HoverTarget> VoxelEditorScene::raycast_hover_target() const
{
    if (_model.voxel_count() == 0)
    {
        return std::nullopt;
    }

    constexpr float maxDistance = 128.0f;
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);

    const VkExtent2D extent = _services.current_window_extent();
    if (extent.width == 0 || extent.height == 0)
    {
        return std::nullopt;
    }

    const glm::mat4 inverseViewProjection = glm::inverse(_camera->_projection * _camera->_view);
    const voxel::picking::Ray ray = voxel::picking::build_ray_from_cursor(
        mouseX,
        mouseY,
        extent,
        _camera->_position,
        inverseViewProjection);

    std::optional<HoverTarget> bestHit{};
    float bestDistance = std::numeric_limits<float>::max();

    for (const auto& [coord, color] : _model.voxels())
    {
        (void)color;
        const glm::vec3 boxMin = (glm::vec3(
            static_cast<float>(coord.x),
            static_cast<float>(coord.y),
            static_cast<float>(coord.z)) * _model.voxelSize) - _model.pivot;
        const glm::vec3 boxMax = boxMin + glm::vec3(_model.voxelSize);

        const auto hit = voxel::picking::intersect_ray_box(ray, boxMin, boxMax, maxDistance);
        if (!hit.has_value() || hit->distance >= bestDistance)
        {
            continue;
        }

        const auto face = voxel::picking::face_from_outward_normal(hit->outwardNormal);
        if (!face.has_value())
        {
            continue;
        }

        bestDistance = hit->distance;
        bestHit = HoverTarget{
            .voxel = coord,
            .face = face.value(),
            .distance = hit->distance
        };
    }

    return bestHit;
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
