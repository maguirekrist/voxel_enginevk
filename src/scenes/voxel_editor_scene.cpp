#include "voxel_editor_scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <SDL.h>

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "editor_shortcuts.h"
#include "imgui.h"
#include "orbit_orientation_gizmo.h"
#include "render/material_manager.h"
#include "render/mesh_manager.h"
#include "render/mesh_release_queue.h"
#include "string_utils.h"
#include "voxel/voxel_orientation.h"
#include "voxel/voxel_picking.h"
#include "voxel/voxel_mesher.h"
#include "voxel/voxel_spatial_bounds.h"
#include "vk_util.h"

namespace
{
    constexpr std::string_view VoxelEditorMaterialScope = "voxel_editor";
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

    glm::vec3 voxel_center_position(const VoxelCoord& voxel, const float voxelSize)
    {
        return (glm::vec3(
            static_cast<float>(voxel.x),
            static_cast<float>(voxel.y),
            static_cast<float>(voxel.z)) + glm::vec3(0.5f)) * voxelSize;
    }

    glm::vec3 voxel_face_center_position(const VoxelCoord& voxel, const FaceDirection face, const float voxelSize)
    {
        const glm::vec3 faceOffset{
            static_cast<float>(faceOffsetX[face]),
            static_cast<float>(faceOffsetY[face]),
            static_cast<float>(faceOffsetZ[face])
        };
        return (glm::vec3(
            static_cast<float>(voxel.x),
            static_cast<float>(voxel.y),
            static_cast<float>(voxel.z)) + glm::vec3(0.5f) + (faceOffset * 0.5f)) * voxelSize;
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
    if (_modelBoundsHandle.has_value())
    {
        _renderState.transparentObjects.remove(_modelBoundsHandle.value());
        _modelBoundsHandle.reset();
    }
    if (_pivotMarkerHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_pivotMarkerHandle.value());
        _pivotMarkerHandle.reset();
    }
    if (_pivotVoxelHandle.has_value())
    {
        _renderState.transparentObjects.remove(_pivotVoxelHandle.value());
        _pivotVoxelHandle.reset();
    }
    if (_selectedAttachmentVoxelHandle.has_value())
    {
        _renderState.transparentObjects.remove(_selectedAttachmentVoxelHandle.value());
        _selectedAttachmentVoxelHandle.reset();
    }
    for (const auto handle : _attachmentMarkerHandles)
    {
        _renderState.opaqueObjects.remove(handle);
    }
    _attachmentMarkerHandles.clear();
    release_preview_mesh();
    release_outline_mesh();
    release_marker_meshes();
}

void VoxelEditorScene::update_buffers()
{
    sync_model_mesh();
    sync_hover_outline();
    sync_marker_overlays();
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
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
    {
        const SDL_Keymod modifiers = SDL_GetModState();
        if (editor_shortcuts::has_primary_modifier(modifiers))
        {
            if (event.key.keysym.sym == SDLK_z)
            {
                if (editor_shortcuts::has_shift_modifier(modifiers))
                {
                    redo_model_edit();
                }
                else
                {
                    undo_model_edit();
                }
                return;
            }

            if (event.key.keysym.sym == SDLK_y)
            {
                redo_model_edit();
                return;
            }

            if (event.key.keysym.sym == SDLK_s)
            {
                save_model();
                return;
            }
        }
    }

    if (event.type == SDL_MOUSEMOTION)
    {
        if (_orbitDragging)
        {
            _orbitCamera.yawDegrees += static_cast<float>(event.motion.xrel) * 0.18f;
            _orbitCamera.pitchDegrees -= static_cast<float>(event.motion.yrel) * 0.18f;
        }
        return;
    }

    if (event.type == SDL_MOUSEWHEEL)
    {
        _orbitCamera.distance = std::clamp(
            _orbitCamera.distance - (static_cast<float>(event.wheel.y) * 0.2f),
            _orbitCamera.minDistance,
            _orbitCamera.maxDistance);
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
            if ((SDL_GetModState() & KMOD_SHIFT) != 0 && try_pick_paint_color_from_hovered_voxel())
            {
                return;
            }

            if (_eraseMode)
            {
                if (_hoveredTarget.has_value() && _model.contains(_hoveredTarget->voxel))
                {
                    const VoxelCoord removedCoord = _hoveredTarget->voxel;
                    apply_model_edit(
                        std::format("Remove voxel {}, {}, {}", removedCoord.x, removedCoord.y, removedCoord.z),
                        [removedCoord](VoxelModel& model)
                        {
                            model.remove_voxel(removedCoord);
                        });
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
                    const VoxelColor placedColor = _paintColor;
                    apply_model_edit(
                        std::format("Place voxel {}, {}, {}", placedCoord.x, placedCoord.y, placedCoord.z),
                        [placedCoord, placedColor](VoxelModel& model)
                        {
                            model.set_voxel(placedCoord, placedColor);
                        });
                }
            }
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            if (_hoveredTarget.has_value() && _model.contains(_hoveredTarget->voxel))
            {
                const VoxelCoord removedCoord = _hoveredTarget->voxel;
                apply_model_edit(
                    std::format("Remove voxel {}, {}, {}", removedCoord.x, removedCoord.y, removedCoord.z),
                    [removedCoord](VoxelModel& model)
                    {
                        model.remove_voxel(removedCoord);
                    });
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
        VoxelEditorMaterialScope,
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
        VoxelEditorMaterialScope,
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Alpha, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST },
        "editor_preview.vert.spv",
        "editor_preview.frag.spv",
        "editorpreview");

    _services.materialManager->build_graphics_pipeline(
        VoxelEditorMaterialScope,
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Alpha, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
        "tri_mesh.vert.spv",
        "tri_mesh.frag.spv",
        "chunkboundary");

    _services.materialManager->build_postprocess_pipeline(VoxelEditorMaterialScope, _fogResource);
    _services.materialManager->build_present_pipeline(VoxelEditorMaterialScope);
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
        .material = _services.materialManager->get_material(VoxelEditorMaterialScope, "defaultmesh"),
        .transform = glm::mat4(1.0f),
        .layer = RenderLayer::Opaque,
        .lightingMode = LightingMode::Unlit
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
        .material = _services.materialManager->get_material(VoxelEditorMaterialScope, "editorpreview"),
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

void VoxelEditorScene::sync_marker_overlays()
{
    if (!_markerDirty)
    {
        return;
    }

    _markerDirty = false;

    if (_pivotMarkerHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_pivotMarkerHandle.value());
        _pivotMarkerHandle.reset();
    }

    if (_pivotVoxelHandle.has_value())
    {
        _renderState.transparentObjects.remove(_pivotVoxelHandle.value());
        _pivotVoxelHandle.reset();
    }
    if (_selectedAttachmentVoxelHandle.has_value())
    {
        _renderState.transparentObjects.remove(_selectedAttachmentVoxelHandle.value());
        _selectedAttachmentVoxelHandle.reset();
    }
    if (_modelBoundsHandle.has_value())
    {
        _renderState.transparentObjects.remove(_modelBoundsHandle.value());
        _modelBoundsHandle.reset();
    }

    for (const auto handle : _attachmentMarkerHandles)
    {
        _renderState.opaqueObjects.remove(handle);
    }
    _attachmentMarkerHandles.clear();
    release_marker_meshes();

    if (_showPivotMarker)
    {
        const float markerSize = std::max(_model.voxelSize * 0.4f, 0.045f);
        _pivotMarkerMesh = Mesh::create_point_marker_mesh(glm::vec3(0.0f), markerSize, glm::vec3(0.30f, 0.88f, 1.0f));
        _services.meshManager->UploadQueue.enqueue(_pivotMarkerMesh);
        _pivotMarkerHandle = _renderState.opaqueObjects.insert(RenderObject{
            .mesh = _pivotMarkerMesh,
            .material = _services.materialManager->get_material(VoxelEditorMaterialScope, "defaultmesh"),
            .transform = glm::mat4(1.0f),
            .layer = RenderLayer::Opaque,
            .lightingMode = LightingMode::Unlit
        });
    }

    if (_showModelBounds)
    {
        const VoxelSpatialBounds modelBounds = evaluate_voxel_model_local_bounds(_model);
        if (modelBounds.valid)
        {
            _modelBoundsMesh = Mesh::create_box_outline_mesh(modelBounds.min, modelBounds.max, glm::vec3(0.30f, 0.88f, 1.0f));
            _services.meshManager->UploadQueue.enqueue(_modelBoundsMesh);
            _modelBoundsHandle = _renderState.transparentObjects.insert(RenderObject{
                .mesh = _modelBoundsMesh,
                .material = _services.materialManager->get_material(VoxelEditorMaterialScope, "chunkboundary"),
                .transform = glm::mat4(1.0f),
                .layer = RenderLayer::Transparent,
                .lightingMode = LightingMode::Unlit
            });
        }
    }

    if (_showPivotVoxel && _model.voxelSize > 0.0f)
    {
        const glm::vec3 pivotVoxelUnits = _model.pivot / _model.voxelSize;
        const VoxelCoord pivotVoxel{
            .x = static_cast<int>(std::floor(pivotVoxelUnits.x)),
            .y = static_cast<int>(std::floor(pivotVoxelUnits.y)),
            .z = static_cast<int>(std::floor(pivotVoxelUnits.z))
        };

        if (is_within_grid(pivotVoxel))
        {
            const glm::vec3 minCorner = (glm::vec3(
                static_cast<float>(pivotVoxel.x),
                static_cast<float>(pivotVoxel.y),
                static_cast<float>(pivotVoxel.z)) * _model.voxelSize) - _model.pivot;
            const float inset = std::min(std::max(_model.voxelSize * 0.1f, 0.003f), _model.voxelSize * 0.45f);
            _pivotVoxelMesh = Mesh::create_box_preview_mesh(
                minCorner + glm::vec3(inset),
                minCorner + glm::vec3(_model.voxelSize - inset),
                glm::vec3(0.35f, 0.62f, 1.0f));
            _services.meshManager->UploadQueue.enqueue(_pivotVoxelMesh);
            _pivotVoxelHandle = _renderState.transparentObjects.insert(RenderObject{
                .mesh = _pivotVoxelMesh,
                .material = _services.materialManager->get_material(VoxelEditorMaterialScope, "editorpreview"),
                .transform = glm::mat4(1.0f),
                .layer = RenderLayer::Transparent,
                .lightingMode = LightingMode::Unlit
            });
        }
    }

    if (_showAttachmentMarkers)
    {
        ensure_attachment_selection();
        _attachmentMarkerMeshes.reserve(_model.attachments.size());
        _attachmentMarkerHandles.reserve(_model.attachments.size());

        for (size_t attachmentIndex = 0; attachmentIndex < _model.attachments.size(); ++attachmentIndex)
        {
            const VoxelAttachment& attachment = _model.attachments[attachmentIndex];
            const bool selected = static_cast<int>(attachmentIndex) == _selectedAttachmentIndex;
            const float markerSize = std::max(_model.voxelSize * (selected ? 0.46f : 0.32f), selected ? 0.055f : 0.04f);
            const glm::vec3 localPosition = attachment.position - _model.pivot;
            std::shared_ptr<Mesh> markerMesh = Mesh::create_point_marker_mesh(
                localPosition,
                markerSize,
                selected ? glm::vec3(1.0f, 0.82f, 0.32f) : glm::vec3(0.46f, 1.0f, 0.46f));
            _services.meshManager->UploadQueue.enqueue(markerMesh);
            _attachmentMarkerHandles.push_back(_renderState.opaqueObjects.insert(RenderObject{
                .mesh = markerMesh,
                .material = _services.materialManager->get_material(VoxelEditorMaterialScope, "defaultmesh"),
                .transform = glm::mat4(1.0f),
                .layer = RenderLayer::Opaque,
                .lightingMode = LightingMode::Unlit
            }));
            _attachmentMarkerMeshes.push_back(std::move(markerMesh));
        }
    }

    if (_showSelectedAttachmentVoxel && _model.voxelSize > 0.0f)
    {
        ensure_attachment_selection();
        if (const VoxelAttachment* const attachment = selected_attachment(); attachment != nullptr)
        {
            const glm::vec3 attachmentVoxelUnits = attachment->position / _model.voxelSize;
            const VoxelCoord attachmentVoxel{
                .x = static_cast<int>(std::floor(attachmentVoxelUnits.x)),
                .y = static_cast<int>(std::floor(attachmentVoxelUnits.y)),
                .z = static_cast<int>(std::floor(attachmentVoxelUnits.z))
            };

            if (is_within_grid(attachmentVoxel))
            {
                const glm::vec3 minCorner = (glm::vec3(
                    static_cast<float>(attachmentVoxel.x),
                    static_cast<float>(attachmentVoxel.y),
                    static_cast<float>(attachmentVoxel.z)) * _model.voxelSize) - _model.pivot;
                const float inset = std::min(std::max(_model.voxelSize * 0.12f, 0.003f), _model.voxelSize * 0.45f);
                _selectedAttachmentVoxelMesh = Mesh::create_box_preview_mesh(
                    minCorner + glm::vec3(inset),
                    minCorner + glm::vec3(_model.voxelSize - inset),
                    glm::vec3(1.0f, 0.78f, 0.24f));
                _services.meshManager->UploadQueue.enqueue(_selectedAttachmentVoxelMesh);
                _selectedAttachmentVoxelHandle = _renderState.transparentObjects.insert(RenderObject{
                    .mesh = _selectedAttachmentVoxelMesh,
                    .material = _services.materialManager->get_material(VoxelEditorMaterialScope, "editorpreview"),
                    .transform = glm::mat4(1.0f),
                    .layer = RenderLayer::Transparent,
                    .lightingMode = LightingMode::Unlit
                });
            }
        }
    }
}

void VoxelEditorScene::release_marker_meshes()
{
    if (_pivotMarkerMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_pivotMarkerMesh));
    }
    if (_modelBoundsMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_modelBoundsMesh));
    }
    if (_pivotVoxelMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_pivotVoxelMesh));
    }
    if (_selectedAttachmentVoxelMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_selectedAttachmentVoxelMesh));
    }
    for (std::shared_ptr<Mesh>& mesh : _attachmentMarkerMeshes)
    {
        if (mesh != nullptr)
        {
            render::enqueue_mesh_release(std::move(mesh));
        }
    }
    _attachmentMarkerMeshes.clear();
}

void VoxelEditorScene::ensure_attachment_selection()
{
    if (_model.attachments.empty())
    {
        _selectedAttachmentIndex = -1;
        return;
    }

    _selectedAttachmentIndex = std::clamp(_selectedAttachmentIndex, 0, static_cast<int>(_model.attachments.size()) - 1);
}

VoxelAttachment* VoxelEditorScene::selected_attachment()
{
    ensure_attachment_selection();
    if (_selectedAttachmentIndex < 0 || _selectedAttachmentIndex >= static_cast<int>(_model.attachments.size()))
    {
        return nullptr;
    }

    return &_model.attachments[static_cast<size_t>(_selectedAttachmentIndex)];
}

const VoxelAttachment* VoxelEditorScene::selected_attachment() const
{
    if (_model.attachments.empty())
    {
        return nullptr;
    }

    const int index = std::clamp(_selectedAttachmentIndex, 0, static_cast<int>(_model.attachments.size()) - 1);
    return &_model.attachments[static_cast<size_t>(index)];
}

std::string VoxelEditorScene::make_unique_attachment_name(const std::string_view baseName) const
{
    std::string sanitized{};
    sanitized.reserve(baseName.size());
    for (const char ch : baseName)
    {
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' ||
            ch == '-')
        {
            sanitized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }

    if (sanitized.empty())
    {
        sanitized = "attachment";
    }

    if (_model.find_attachment(sanitized) == nullptr)
    {
        return sanitized;
    }

    for (int suffix = 2;; ++suffix)
    {
        const std::string candidate = std::format("{}_{}", sanitized, suffix);
        if (_model.find_attachment(candidate) == nullptr)
        {
            return candidate;
        }
    }
}

void VoxelEditorScene::update_camera()
{
    editor::update_orbit_camera(*_camera, orbit_target(), _orbitCamera);
}

void VoxelEditorScene::draw_orientation_gizmo()
{
    (void)editor::draw_orbit_orientation_gizmo(*_camera, orbit_target(), _orbitCamera);
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

    ImGui::Text("Scene Switch: F1 Game | F2 Voxel Editor | F3 Voxel Assembly");
    ImGui::SeparatorText("Asset");

    char assetIdBuffer[128]{};
    char displayNameBuffer[128]{};
    copy_cstr_truncating(assetIdBuffer, _model.assetId);
    copy_cstr_truncating(displayNameBuffer, _model.displayName);

    if (ImGui::InputText("Asset Id", assetIdBuffer, IM_ARRAYSIZE(assetIdBuffer)))
    {
        const std::string nextAssetId = assetIdBuffer;
        apply_model_edit("Rename voxel asset id", [nextAssetId](VoxelModel& model)
        {
            model.assetId = nextAssetId;
        });
    }
    if (ImGui::InputText("Display Name", displayNameBuffer, IM_ARRAYSIZE(displayNameBuffer)))
    {
        const std::string nextDisplayName = displayNameBuffer;
        apply_model_edit("Rename voxel display name", [nextDisplayName](VoxelModel& model)
        {
            model.displayName = nextDisplayName;
        });
    }
    float voxelSize = _model.voxelSize;
    if (ImGui::InputFloat("Voxel Size", &voxelSize, 0.0f, 0.0f, "%.4f"))
    {
        const float nextVoxelSize = std::max(voxelSize, 0.001f);
        apply_model_edit("Change voxel size", [nextVoxelSize](VoxelModel& model)
        {
            model.voxelSize = nextVoxelSize;
        });
    }

    glm::vec3 pivotVoxelUnits = _model.pivot / _model.voxelSize;
    if (ImGui::InputFloat3("Pivot (Voxel Units)", &pivotVoxelUnits.x, "%.3f"))
    {
        const glm::vec3 nextPivot = pivotVoxelUnits * _model.voxelSize;
        apply_model_edit("Move pivot", [nextPivot](VoxelModel& model)
        {
            model.pivot = nextPivot;
        });
    }

    if (ImGui::Button("Pivot To Grid Center XZ"))
    {
        const glm::vec3 nextPivot = glm::vec3(
            static_cast<float>(_gridDimensions.x) * 0.5f * _model.voxelSize,
            _model.pivot.y,
            static_cast<float>(_gridDimensions.z) * 0.5f * _model.voxelSize);
        apply_model_edit("Move pivot to grid center XZ", [nextPivot](VoxelModel& model)
        {
            model.pivot = nextPivot;
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Pivot To Bounds Center"))
    {
        const glm::vec3 nextPivot = _model.bounds().center() * _model.voxelSize;
        apply_model_edit("Move pivot to bounds center", [nextPivot](VoxelModel& model)
        {
            model.pivot = nextPivot;
        });
    }

    if (ImGui::Checkbox("Show Pivot Marker", &_showPivotMarker))
    {
        _markerDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Model Bounds", &_showModelBounds))
    {
        _markerDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Pivot Voxel", &_showPivotVoxel))
    {
        _markerDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Attachment Markers", &_showAttachmentMarkers))
    {
        _markerDirty = true;
    }

    ImGui::TextDisabled("Orientation Standard: +X forward, +Y up. Pivot is the local transform origin and may be outside the mesh.");
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Selected Attachment Voxel", &_showSelectedAttachmentVoxel))
    {
        _markerDirty = true;
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
    ImGui::SameLine();
    if (! _history.can_undo())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Undo"))
    {
        undo_model_edit();
    }
    if (! _history.can_undo())
    {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (! _history.can_redo())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Redo"))
    {
        redo_model_edit();
    }
    if (! _history.can_redo())
    {
        ImGui::EndDisabled();
    }
    ImGui::TextDisabled(
        "History: %zu undo / %zu redo (Ctrl+Z / Ctrl+Y, Ctrl+Shift+Z)",
        _history.undo_count(),
        _history.redo_count());

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

    ImGui::SeparatorText("Attachments");
    ImGui::TextWrapped("Attachments are named socket/anchor points stored on this voxel model. Assemblies bind children against these names.");

    if (ImGui::Button("Add Attachment"))
    {
        sync_hover_target();

        VoxelAttachment attachment{};
        attachment.name = make_unique_attachment_name("attachment");
        attachment.position = _model.pivot;

        if (_hoveredTarget.has_value())
        {
            attachment.position = voxel_face_center_position(_hoveredTarget->voxel, _hoveredTarget->face, _model.voxelSize);
            attachment.forward = voxel::orientation::normalize_or_fallback(glm::vec3(
                static_cast<float>(faceOffsetX[_hoveredTarget->face]),
                static_cast<float>(faceOffsetY[_hoveredTarget->face]),
                static_cast<float>(faceOffsetZ[_hoveredTarget->face])),
                voxel::orientation::ForwardAxis);
        }
        voxel::orientation::sanitize_attachment_basis(attachment);

        const std::string attachmentName = attachment.name;
        apply_model_edit(
            std::format("Add attachment '{}'", attachmentName),
            [attachment = std::move(attachment)](VoxelModel& model) mutable
            {
                model.attachments.push_back(std::move(attachment));
            });
        _selectedAttachmentIndex = static_cast<int>(_model.attachments.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Selected Attachment"))
    {
        if (const VoxelAttachment* const attachment = selected_attachment(); attachment != nullptr)
        {
            const std::string removedName = attachment->name;
            const int removedIndex = _selectedAttachmentIndex;
            apply_model_edit(
                std::format("Remove attachment '{}'", removedName),
                [removedIndex](VoxelModel& model)
                {
                    if (removedIndex >= 0 && removedIndex < static_cast<int>(model.attachments.size()))
                    {
                        model.attachments.erase(model.attachments.begin() + removedIndex);
                    }
                });
        }
    }

    if (ImGui::BeginChild("AttachmentList", ImVec2(0.0f, 120.0f), true))
    {
        if (_model.attachments.empty())
        {
            ImGui::TextDisabled("No attachments authored on this model.");
        }
        else
        {
            for (int attachmentIndex = 0; attachmentIndex < static_cast<int>(_model.attachments.size()); ++attachmentIndex)
            {
                ImGui::PushID(attachmentIndex);
                const VoxelAttachment& attachment = _model.attachments[static_cast<size_t>(attachmentIndex)];
                const bool selected = attachmentIndex == _selectedAttachmentIndex;
                const char* label = attachment.name.empty() ? "<unnamed attachment>" : attachment.name.c_str();
                if (ImGui::Selectable(label, selected))
                {
                    _selectedAttachmentIndex = attachmentIndex;
                    _markerDirty = true;
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();

    if (VoxelAttachment* const attachment = selected_attachment(); attachment != nullptr)
    {
        char attachmentNameBuffer[128]{};
        copy_cstr_truncating(attachmentNameBuffer, attachment->name);
        if (ImGui::InputText("Attachment Name", attachmentNameBuffer, IM_ARRAYSIZE(attachmentNameBuffer)))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const std::string nextName = attachmentNameBuffer;
            apply_model_edit("Rename attachment", [attachmentIndex, nextName](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].name = nextName;
                }
            });
        }

        bool duplicateAttachmentName = false;
        if (!attachment->name.empty())
        {
            int duplicateCount = 0;
            for (const VoxelAttachment& otherAttachment : _model.attachments)
            {
                if (otherAttachment.name == attachment->name)
                {
                    ++duplicateCount;
                }
            }
            duplicateAttachmentName = duplicateCount > 1;
        }

        if (attachment->name.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.38f, 1.0f), "Attachment name is empty. Assemblies will not be able to target it.");
        }
        else if (duplicateAttachmentName)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.38f, 1.0f), "Attachment name '%s' is duplicated on this model.", attachment->name.c_str());
        }

        glm::vec3 attachmentVoxelUnits = _model.voxelSize > 0.0f
            ? attachment->position / _model.voxelSize
            : glm::vec3(0.0f);
        if (ImGui::InputFloat3("Attachment Position (Voxel Units)", &attachmentVoxelUnits.x, "%.3f"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextPosition = attachmentVoxelUnits * _model.voxelSize;
            apply_model_edit("Move attachment", [attachmentIndex, nextPosition](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                }
            });
        }

        sync_hover_target();
        if (ImGui::Button("Set Attachment To Pivot"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextPosition = _model.pivot;
            apply_model_edit("Move attachment to pivot", [attachmentIndex, nextPosition](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                }
            });
        }
        ImGui::SameLine();
        if (_hoveredTarget.has_value())
        {
            if (ImGui::Button("Set To Hovered Voxel"))
            {
                const int attachmentIndex = _selectedAttachmentIndex;
                const glm::vec3 nextPosition = voxel_center_position(_hoveredTarget->voxel, _model.voxelSize);
                apply_model_edit("Move attachment to hovered voxel", [attachmentIndex, nextPosition](VoxelModel& model)
                {
                    if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                    {
                        model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                    }
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Set To Hovered Face"))
            {
                const int attachmentIndex = _selectedAttachmentIndex;
                const glm::vec3 nextPosition = voxel_face_center_position(_hoveredTarget->voxel, _hoveredTarget->face, _model.voxelSize);
                apply_model_edit("Move attachment to hovered face", [attachmentIndex, nextPosition](VoxelModel& model)
                {
                    if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                    {
                        model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                    }
                });
            }
        }
        else
        {
            ImGui::BeginDisabled();
            ImGui::Button("Set To Hovered Voxel");
            ImGui::SameLine();
            ImGui::Button("Set To Hovered Face");
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Set To Bounds Center"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextPosition = _model.bounds().center() * _model.voxelSize;
            apply_model_edit("Move attachment to bounds center", [attachmentIndex, nextPosition](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                }
            });
        }

        ImGui::Text("Attachment Nudge: 1 voxel (%.4f world units)", _model.voxelSize);
        if (ImGui::Button("-X##AttachmentNudge"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextPosition = attachment->position + glm::vec3(-_model.voxelSize, 0.0f, 0.0f);
            apply_model_edit("Nudge attachment -X", [attachmentIndex, nextPosition](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("+X##AttachmentNudge"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextPosition = attachment->position + glm::vec3(_model.voxelSize, 0.0f, 0.0f);
            apply_model_edit("Nudge attachment +X", [attachmentIndex, nextPosition](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("-Y##AttachmentNudge"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextPosition = attachment->position + glm::vec3(0.0f, -_model.voxelSize, 0.0f);
            apply_model_edit("Nudge attachment -Y", [attachmentIndex, nextPosition](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("+Y##AttachmentNudge"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextPosition = attachment->position + glm::vec3(0.0f, _model.voxelSize, 0.0f);
            apply_model_edit("Nudge attachment +Y", [attachmentIndex, nextPosition](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("-Z##AttachmentNudge"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextPosition = attachment->position + glm::vec3(0.0f, 0.0f, -_model.voxelSize);
            apply_model_edit("Nudge attachment -Z", [attachmentIndex, nextPosition](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("+Z##AttachmentNudge"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextPosition = attachment->position + glm::vec3(0.0f, 0.0f, _model.voxelSize);
            apply_model_edit("Nudge attachment +Z", [attachmentIndex, nextPosition](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].position = nextPosition;
                }
            });
        }

        ImGui::TextWrapped("Forward/Up define the attachment orientation that assembly child bindings inherit.");
        glm::vec3 attachmentForward = attachment->forward;
        if (ImGui::InputFloat3("Attachment Forward", &attachmentForward.x, "%.3f"))
        {
            VoxelAttachment preview = *attachment;
            preview.forward = attachmentForward;
            voxel::orientation::sanitize_attachment_basis(preview);
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextForward = preview.forward;
            const glm::vec3 nextUp = preview.up;
            apply_model_edit("Edit attachment forward", [attachmentIndex, nextForward, nextUp](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    VoxelAttachment& selectedAttachment = model.attachments[static_cast<size_t>(attachmentIndex)];
                    selectedAttachment.forward = nextForward;
                    selectedAttachment.up = nextUp;
                }
            });
        }
        if (ImGui::Button("+X##AttachmentForward"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.forward = glm::vec3(1.0f, 0.0f, 0.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment forward +X", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("-X##AttachmentForward"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.forward = glm::vec3(-1.0f, 0.0f, 0.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment forward -X", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("+Y##AttachmentForward"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.forward = glm::vec3(0.0f, 1.0f, 0.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment forward +Y", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("-Y##AttachmentForward"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.forward = glm::vec3(0.0f, -1.0f, 0.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment forward -Y", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("+Z##AttachmentForward"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.forward = glm::vec3(0.0f, 0.0f, 1.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment forward +Z", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("-Z##AttachmentForward"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.forward = glm::vec3(0.0f, 0.0f, -1.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment forward -Z", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }

        glm::vec3 attachmentUp = attachment->up;
        if (ImGui::InputFloat3("Attachment Up", &attachmentUp.x, "%.3f"))
        {
            VoxelAttachment preview = *attachment;
            preview.up = attachmentUp;
            voxel::orientation::sanitize_attachment_basis(preview);
            const int attachmentIndex = _selectedAttachmentIndex;
            const glm::vec3 nextForward = preview.forward;
            const glm::vec3 nextUp = preview.up;
            apply_model_edit("Edit attachment up", [attachmentIndex, nextForward, nextUp](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    VoxelAttachment& selectedAttachment = model.attachments[static_cast<size_t>(attachmentIndex)];
                    selectedAttachment.forward = nextForward;
                    selectedAttachment.up = nextUp;
                }
            });
        }
        if (ImGui::Button("+X##AttachmentUp"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.up = glm::vec3(1.0f, 0.0f, 0.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment up +X", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("-X##AttachmentUp"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.up = glm::vec3(-1.0f, 0.0f, 0.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment up -X", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("+Y##AttachmentUp"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.up = glm::vec3(0.0f, 1.0f, 0.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment up +Y", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("-Y##AttachmentUp"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.up = glm::vec3(0.0f, -1.0f, 0.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment up -Y", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("+Z##AttachmentUp"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.up = glm::vec3(0.0f, 0.0f, 1.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment up +Z", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("-Z##AttachmentUp"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            preview.up = glm::vec3(0.0f, 0.0f, -1.0f);
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Set attachment up -Z", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }

        if (ImGui::Button("Normalize Attachment Basis"))
        {
            const int attachmentIndex = _selectedAttachmentIndex;
            VoxelAttachment preview = *attachment;
            voxel::orientation::sanitize_attachment_basis(preview);
            apply_model_edit("Normalize attachment basis", [attachmentIndex, preview](VoxelModel& model)
            {
                if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                {
                    model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                    model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                }
            });
        }

        if (_hoveredTarget.has_value())
        {
            ImGui::SameLine();
            if (ImGui::Button("Forward = Hovered Face"))
            {
                VoxelAttachment preview = *attachment;
                preview.forward = voxel::orientation::normalize_or_fallback(glm::vec3(
                    static_cast<float>(faceOffsetX[_hoveredTarget->face]),
                    static_cast<float>(faceOffsetY[_hoveredTarget->face]),
                    static_cast<float>(faceOffsetZ[_hoveredTarget->face])),
                    voxel::orientation::ForwardAxis);
                voxel::orientation::sanitize_attachment_basis(preview);
                const int attachmentIndex = _selectedAttachmentIndex;
                apply_model_edit("Set attachment forward from hovered face", [attachmentIndex, preview](VoxelModel& model)
                {
                    if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(model.attachments.size()))
                    {
                        model.attachments[static_cast<size_t>(attachmentIndex)].forward = preview.forward;
                        model.attachments[static_cast<size_t>(attachmentIndex)].up = preview.up;
                    }
                });
            }
        }
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
    ImGui::Text("Editor Controls: MMB drag orbit, wheel zoom, LMB place, RMB remove, Shift+LMB pick color.");
    ImGui::Text("Placement/removal uses the actual mouse cursor over the 3D view.");
    ImGui::SliderFloat("Yaw", &_orbitCamera.yawDegrees, -180.0f, 180.0f, "%.1f deg");
    ImGui::SliderFloat("Pitch", &_orbitCamera.pitchDegrees, -80.0f, 80.0f, "%.1f deg");
    ImGui::SliderFloat("Distance", &_orbitCamera.distance, _orbitCamera.minDistance, 48.0f, "%.2f");

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
                    if (_model.contains(voxelCoord))
                    {
                        apply_model_edit(
                            std::format("Remove voxel {}, {}, {}", voxelCoord.x, voxelCoord.y, voxelCoord.z),
                            [voxelCoord](VoxelModel& model)
                            {
                            model.remove_voxel(voxelCoord);
                        });
                }
            }
            else
            {
                const VoxelColor paintedColor = _paintColor;
                apply_model_edit(
                    std::format("Paint voxel {}, {}, {}", voxelCoord.x, voxelCoord.y, voxelCoord.z),
                    [voxelCoord, paintedColor](VoxelModel& model)
                    {
                        model.set_voxel(voxelCoord, paintedColor);
                    });
            }
        }
                else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                {
                    if (_model.contains(voxelCoord))
                    {
                        apply_model_edit(
                            std::format("Remove voxel {}, {}, {}", voxelCoord.x, voxelCoord.y, voxelCoord.z),
                            [voxelCoord](VoxelModel& model)
                            {
                        model.remove_voxel(voxelCoord);
                    });
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
    _markerDirty = true;

    if (_outlineHandle.has_value())
    {
        _renderState.transparentObjects.remove(_outlineHandle.value());
        _outlineHandle.reset();
    }
    release_outline_mesh();
    _outlinedVoxelCoord.reset();
    _outlineShowsRemoval = false;

}

void VoxelEditorScene::apply_model_edit(const std::string_view description, const std::function<void(VoxelModel&)>& edit)
{
    (void)_editorSession.apply(description, edit, [this]()
    {
        mark_model_dirty();
        sync_hover_target();
        ensure_attachment_selection();
    }, &_statusMessage);
}

void VoxelEditorScene::undo_model_edit()
{
    (void)_editorSession.undo([this]()
    {
        mark_model_dirty();
        sync_hover_target();
        ensure_attachment_selection();
    }, &_statusMessage);
}

void VoxelEditorScene::redo_model_edit()
{
    (void)_editorSession.redo([this]()
    {
        mark_model_dirty();
        sync_hover_target();
        ensure_attachment_selection();
    }, &_statusMessage);
}

void VoxelEditorScene::reset_model()
{
    _model = VoxelModel{};
    _history.clear();
    _model.assetId = "untitled";
    _model.displayName = "Untitled";
    _model.voxelSize = 1.0f / 16.0f;
    _model.set_voxel(VoxelCoord{ 0, 0, 0 }, _paintColor);
    _gridDimensions = glm::ivec3(16, 16, 16);
    _editAxis = EditAxis::Z;
    _sliceIndex = 0;
    _rotationQuarterTurns = 0;
    _selectedAttachmentIndex = -1;
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
        _history.clear();
        _selectedAttachmentIndex = _model.attachments.empty() ? -1 : 0;
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

bool VoxelEditorScene::try_pick_paint_color_from_hovered_voxel()
{
    if (!_hoveredTarget.has_value())
    {
        return false;
    }

    const VoxelColor* const hoveredColor = _model.try_get(_hoveredTarget->voxel);
    if (hoveredColor == nullptr)
    {
        return false;
    }

    _paintColor = *hoveredColor;
    _statusMessage = std::format(
        "Picked paint color rgba({}, {}, {}, {}) from voxel {}, {}, {}",
        static_cast<int>(_paintColor.r),
        static_cast<int>(_paintColor.g),
        static_cast<int>(_paintColor.b),
        static_cast<int>(_paintColor.a),
        _hoveredTarget->voxel.x,
        _hoveredTarget->voxel.y,
        _hoveredTarget->voxel.z);
    return true;
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
