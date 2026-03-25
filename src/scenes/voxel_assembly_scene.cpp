#include "voxel_assembly_scene.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
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
#include "vk_util.h"

namespace
{
    constexpr std::string_view VoxelAssemblyMaterialScope = "voxel_assembly";
    constexpr glm::vec3 EditorBackgroundColor{0.06f, 0.07f, 0.09f};
    constexpr glm::vec3 EditorFogColor{0.12f, 0.14f, 0.17f};

    glm::vec3 orbit_front(const float yawDegrees, const float pitchDegrees)
    {
        const float yaw = glm::radians(yawDegrees);
        const float pitch = glm::radians(pitchDegrees);
        return glm::normalize(glm::vec3(
            std::cos(pitch) * std::cos(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::sin(yaw)));
    }

    glm::quat basis_from_attachment(const VoxelAttachment& attachment)
    {
        const glm::vec3 forward = glm::length(attachment.forward) > 0.0001f
            ? glm::normalize(attachment.forward)
            : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 up = glm::length(attachment.up) > 0.0001f
            ? glm::normalize(attachment.up)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::cross(up, forward);

        if (glm::length(right) <= 0.0001f)
        {
            right = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            right = glm::normalize(right);
        }

        up = glm::normalize(glm::cross(forward, right));

        glm::mat3 basis{1.0f};
        basis[0] = forward;
        basis[1] = up;
        basis[2] = right;
        return glm::normalize(glm::quat_cast(basis));
    }

    float uniform_scale_from_vec3(const glm::vec3& scale)
    {
        return (scale.x + scale.y + scale.z) / 3.0f;
    }

    glm::vec3 euler_degrees_from_quat(const glm::quat& rotation)
    {
        return glm::degrees(glm::eulerAngles(rotation));
    }
}

VoxelAssemblyScene::VoxelAssemblyScene(const SceneServices& services) :
    _services(services),
    _documentStore(),
    _modelRepository(_documentStore),
    _assemblyRepository(_documentStore),
    _assetManager(_modelRepository)
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

    _camera = std::make_unique<Camera>(glm::vec3(0.0f, 1.5f, -6.0f), _services.current_window_extent());

    reset_assembly();
    refresh_saved_assets();
    build_pipelines();
    update_camera();
    update_uniform_buffers();
}

VoxelAssemblyScene::~VoxelAssemblyScene()
{
    clear_preview();
    if (_selectedPartBoundsHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_selectedPartBoundsHandle.value());
        _selectedPartBoundsHandle.reset();
    }
    if (_selectedPartPivotHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_selectedPartPivotHandle.value());
        _selectedPartPivotHandle.reset();
    }
    if (_parentAttachmentMarkerHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_parentAttachmentMarkerHandle.value());
        _parentAttachmentMarkerHandle.reset();
    }
    for (const auto handle : _selectedAttachmentMarkerHandles)
    {
        _renderState.opaqueObjects.remove(handle);
    }
    _selectedAttachmentMarkerHandles.clear();
    release_selection_meshes();
}

void VoxelAssemblyScene::update_buffers()
{
    sync_preview_instances();
    _previewRenderRegistry.sync(*_services.meshManager, *_services.materialManager, VoxelAssemblyMaterialScope, _renderState);
    sync_selection_overlay();
    update_uniform_buffers();
}

void VoxelAssemblyScene::update(const float deltaTime)
{
    (void)deltaTime;
    update_camera();
}

void VoxelAssemblyScene::handle_input(const SDL_Event& event)
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
                    redo_assembly_edit();
                }
                else
                {
                    undo_assembly_edit();
                }
                return;
            }

            if (event.key.keysym.sym == SDLK_y)
            {
                redo_assembly_edit();
                return;
            }

            if (event.key.keysym.sym == SDLK_s)
            {
                save_assembly();
                return;
            }
        }
    }

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
        _orbitDistance = std::clamp(_orbitDistance - (static_cast<float>(event.wheel.y) * 0.25f), 0.75f, 64.0f);
        return;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_MIDDLE)
    {
        _orbitDragging = true;
    }
    else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_MIDDLE)
    {
        _orbitDragging = false;
    }
}

void VoxelAssemblyScene::handle_keystate(const Uint8* state)
{
    (void)state;
}

void VoxelAssemblyScene::clear_input()
{
    _orbitDragging = false;
}

void VoxelAssemblyScene::draw_imgui()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    draw_editor_window();
    draw_orientation_gizmo();
    ImGui::Render();
}

void VoxelAssemblyScene::build_pipelines()
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
        VoxelAssemblyMaterialScope,
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
        VoxelAssemblyMaterialScope,
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Alpha, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST },
        "editor_preview.vert.spv",
        "editor_preview.frag.spv",
        "editorpreview");

    _services.materialManager->build_graphics_pipeline(
        VoxelAssemblyMaterialScope,
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Opaque, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
        "tri_mesh.vert.spv",
        "tri_mesh.frag.spv",
        "chunkboundary");

    _services.materialManager->build_postprocess_pipeline(VoxelAssemblyMaterialScope, _fogResource);
    _services.materialManager->build_present_pipeline(VoxelAssemblyMaterialScope);
}

void VoxelAssemblyScene::rebuild_pipelines()
{
    _camera->resize(_services.current_window_extent());
    build_pipelines();
}

SceneRenderState& VoxelAssemblyScene::get_render_state()
{
    return _renderState;
}

void VoxelAssemblyScene::update_uniform_buffers() const
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

void VoxelAssemblyScene::update_camera()
{
    _orbitPitchDegrees = std::clamp(_orbitPitchDegrees, -85.0f, 85.0f);
    _orbitDistance = std::clamp(_orbitDistance, 0.75f, 64.0f);

    const glm::vec3 front = orbit_front(_orbitYawDegrees, _orbitPitchDegrees);
    const glm::vec3 target = orbit_target();
    _camera->_front = front;
    _camera->_up = glm::vec3(0.0f, 1.0f, 0.0f);
    _camera->_position = target - (front * _orbitDistance);
    _camera->update(0.0f);
}

void VoxelAssemblyScene::draw_orientation_gizmo()
{
    glm::mat4 gizmoView = _camera->_view;
    if (draw_orbit_orientation_gizmo(gizmoView, _orbitDistance))
    {
        sync_orbit_from_view_matrix(gizmoView);
    }
}

void VoxelAssemblyScene::sync_orbit_from_view_matrix(const glm::mat4& viewMatrix)
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

void VoxelAssemblyScene::sync_preview_instances()
{
    if (_previewDirty)
    {
        clear_preview();
        release_selection_meshes();
        _previewDirty = false;
        _previewOrbitTarget = glm::vec3(0.0f);
        _resolvedPreviewInstances.clear();

        std::unordered_map<std::string, VoxelRenderInstance> resolvedInstances{};
        std::unordered_set<std::string> visiting{};
        glm::vec3 targetAccumulator{0.0f};
        size_t targetCount = 0;

        std::function<bool(const VoxelAssemblyPartDefinition&, VoxelRenderInstance&)> resolvePart =
            [&](const VoxelAssemblyPartDefinition& part, VoxelRenderInstance& outInstance) -> bool
        {
            if (const auto it = resolvedInstances.find(part.partId); it != resolvedInstances.end())
            {
                outInstance = it->second;
                return true;
            }

            if (!visiting.insert(part.partId).second)
            {
                _statusMessage = std::format("Cycle detected while resolving preview part '{}'", part.partId);
                return false;
            }

            const std::shared_ptr<VoxelRuntimeAsset> asset = _assetManager.load_or_get(part.defaultModelAssetId);
            if (asset == nullptr)
            {
                visiting.erase(part.partId);
                _statusMessage = std::format("Missing voxel model '{}'", part.defaultModelAssetId);
                return false;
            }

            VoxelRenderInstance instance{};
            instance.asset = asset;
            instance.layer = RenderLayer::Opaque;
            instance.lightingMode = LightingMode::Unlit;
            instance.visible = part.visibleByDefault;

            if (const VoxelAssemblyBindingState* const bindingState = preview_binding_state(part);
                bindingState != nullptr)
            {
                instance.visible = instance.visible && bindingState->visible;
                const bool isRoot = part.partId == _assembly.rootPartId || _assembly.rootPartId.empty();
                if (isRoot || bindingState->parentPartId.empty())
                {
                    instance.scale = uniform_scale_from_vec3(bindingState->localScale);
                    instance.rotation = bindingState->localRotationOffset;
                    instance.position = bindingState->localPositionOffset;
                }
                else if (const VoxelAssemblyPartDefinition* const parentPart = _assembly.find_part(bindingState->parentPartId);
                    parentPart != nullptr)
                {
                    VoxelRenderInstance parentInstance{};
                    if (resolvePart(*parentPart, parentInstance))
                    {
                        instance.scale = parentInstance.scale * uniform_scale_from_vec3(bindingState->localScale);

                        if (const VoxelAttachment* const attachment =
                            parentInstance.asset->model.find_attachment(bindingState->parentAttachmentName);
                            attachment != nullptr)
                        {
                            const glm::quat attachmentRotation = basis_from_attachment(*attachment);
                            const glm::quat parentAttachmentRotation = parentInstance.rotation * attachmentRotation;
                            instance.rotation = parentAttachmentRotation * bindingState->localRotationOffset;
                            instance.position = parentInstance.world_point_from_asset_local(attachment->position) +
                                (parentAttachmentRotation * (bindingState->localPositionOffset * parentInstance.scale));
                        }
                        else
                        {
                            instance.rotation = parentInstance.rotation * bindingState->localRotationOffset;
                            instance.position = parentInstance.position +
                                (parentInstance.rotation * (bindingState->localPositionOffset * parentInstance.scale));
                        }
                    }
                }
            }

            visiting.erase(part.partId);
            resolvedInstances.insert_or_assign(part.partId, instance);
            _resolvedPreviewInstances.insert_or_assign(part.partId, instance);
            outInstance = instance;
            return true;
        };

        for (const VoxelAssemblyPartDefinition& part : _assembly.parts)
        {
            VoxelRenderInstance instance{};
            if (!resolvePart(part, instance) || !instance.is_renderable())
            {
                continue;
            }

            const VoxelRenderRegistry::InstanceId instanceId = _previewRenderRegistry.add_instance(instance);
            (void)instanceId;

            if (instance.asset->bounds.valid)
            {
                const glm::vec3 boundsCenter = instance.asset->model.bounds().center() * instance.asset->model.voxelSize;
                targetAccumulator += instance.world_point_from_asset_local(boundsCenter);
                ++targetCount;
            }
            else
            {
                targetAccumulator += instance.position;
                ++targetCount;
            }
        }

        if (targetCount > 0)
        {
            _previewOrbitTarget = targetAccumulator / static_cast<float>(targetCount);
        }
    }
}

void VoxelAssemblyScene::sync_selection_overlay()
{
    if (!_selectionOverlayDirty)
    {
        return;
    }

    _selectionOverlayDirty = false;

    if (_selectedPartBoundsHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_selectedPartBoundsHandle.value());
        _selectedPartBoundsHandle.reset();
    }
    if (_selectedPartPivotHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_selectedPartPivotHandle.value());
        _selectedPartPivotHandle.reset();
    }
    if (_parentAttachmentMarkerHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_parentAttachmentMarkerHandle.value());
        _parentAttachmentMarkerHandle.reset();
    }
    for (const auto handle : _selectedAttachmentMarkerHandles)
    {
        _renderState.opaqueObjects.remove(handle);
    }
    _selectedAttachmentMarkerHandles.clear();
    release_selection_meshes();

    const VoxelAssemblyPartDefinition* const part = selected_part();
    if (part == nullptr)
    {
        return;
    }

    const auto instanceIt = _resolvedPreviewInstances.find(part->partId);
    if (instanceIt == _resolvedPreviewInstances.end() || !instanceIt->second.is_renderable())
    {
        return;
    }

    const VoxelRenderInstance& instance = instanceIt->second;
    if (_showSelectedPartBounds && instance.asset->bounds.valid)
    {
        const VoxelBounds bounds = instance.asset->model.bounds();
        const glm::vec3 minCorner = glm::vec3(
            static_cast<float>(bounds.min.x),
            static_cast<float>(bounds.min.y),
            static_cast<float>(bounds.min.z)) * instance.asset->model.voxelSize - instance.asset->model.pivot;
        const glm::vec3 maxCorner = glm::vec3(
            static_cast<float>(bounds.max.x + 1),
            static_cast<float>(bounds.max.y + 1),
            static_cast<float>(bounds.max.z + 1)) * instance.asset->model.voxelSize - instance.asset->model.pivot;
        _selectedPartBoundsMesh = Mesh::create_box_outline_mesh(minCorner, maxCorner, glm::vec3(0.28f, 0.92f, 1.0f));
        _services.meshManager->UploadQueue.enqueue(_selectedPartBoundsMesh);
        _selectedPartBoundsHandle = _renderState.opaqueObjects.insert(RenderObject{
            .mesh = _selectedPartBoundsMesh,
            .material = _services.materialManager->get_material(VoxelAssemblyMaterialScope, "chunkboundary"),
            .transform = instance.model_matrix(),
            .layer = RenderLayer::Opaque,
            .lightingMode = LightingMode::Unlit
        });
    }

    if (_showSelectedPartPivot)
    {
        const float markerSize = std::max(instance.asset->model.voxelSize * 0.4f, 0.045f);
        _selectedPartPivotMesh = Mesh::create_point_marker_mesh(glm::vec3(0.0f), markerSize, glm::vec3(0.28f, 0.92f, 1.0f));
        _services.meshManager->UploadQueue.enqueue(_selectedPartPivotMesh);
        _selectedPartPivotHandle = _renderState.opaqueObjects.insert(RenderObject{
            .mesh = _selectedPartPivotMesh,
            .material = _services.materialManager->get_material(VoxelAssemblyMaterialScope, "defaultmesh"),
            .transform = instance.model_matrix(),
            .layer = RenderLayer::Opaque,
            .lightingMode = LightingMode::Unlit
        });
    }

    if (_showSelectedPartAttachments)
    {
        _selectedAttachmentMarkerMeshes.reserve(instance.asset->model.attachments.size());
        _selectedAttachmentMarkerHandles.reserve(instance.asset->model.attachments.size());

        for (const VoxelAttachment& attachment : instance.asset->model.attachments)
        {
            const float markerSize = std::max(instance.asset->model.voxelSize * 0.32f, 0.04f);
            const glm::vec3 localPosition = attachment.position - instance.asset->model.pivot;
            std::shared_ptr<Mesh> markerMesh = Mesh::create_point_marker_mesh(
                localPosition,
                markerSize,
                glm::vec3(0.46f, 1.0f, 0.46f));
            _services.meshManager->UploadQueue.enqueue(markerMesh);
            _selectedAttachmentMarkerHandles.push_back(_renderState.opaqueObjects.insert(RenderObject{
                .mesh = markerMesh,
                .material = _services.materialManager->get_material(VoxelAssemblyMaterialScope, "defaultmesh"),
                .transform = instance.model_matrix(),
                .layer = RenderLayer::Opaque,
                .lightingMode = LightingMode::Unlit
            }));
            _selectedAttachmentMarkerMeshes.push_back(std::move(markerMesh));
        }
    }

    VoxelAssemblyPartDefinition* const selectedPart = selected_part();
    const VoxelAssemblyBindingState* const bindingState =
        selectedPart != nullptr ? selected_binding_state(*selectedPart) : nullptr;
    if (_showParentAttachmentMarker &&
        bindingState != nullptr &&
        !bindingState->parentPartId.empty() &&
        !bindingState->parentAttachmentName.empty())
    {
        const auto parentInstanceIt = _resolvedPreviewInstances.find(bindingState->parentPartId);
        if (parentInstanceIt != _resolvedPreviewInstances.end())
        {
            const VoxelRenderInstance& parentInstance = parentInstanceIt->second;
            if (const std::optional<glm::mat4> attachmentTransform =
                parentInstance.attachment_world_transform(bindingState->parentAttachmentName);
                attachmentTransform.has_value())
            {
                const float markerSize = std::max(parentInstance.asset->model.voxelSize * 0.38f, 0.05f);
                _parentAttachmentMarkerMesh = Mesh::create_point_marker_mesh(
                    glm::vec3(0.0f),
                    markerSize,
                    glm::vec3(1.0f, 0.82f, 0.32f));
                _services.meshManager->UploadQueue.enqueue(_parentAttachmentMarkerMesh);
                _parentAttachmentMarkerHandle = _renderState.opaqueObjects.insert(RenderObject{
                    .mesh = _parentAttachmentMarkerMesh,
                    .material = _services.materialManager->get_material(VoxelAssemblyMaterialScope, "defaultmesh"),
                    .transform = attachmentTransform.value(),
                    .layer = RenderLayer::Opaque,
                    .lightingMode = LightingMode::Unlit
                });
            }
        }
    }
}

void VoxelAssemblyScene::clear_preview()
{
    _previewRenderRegistry.clear(_renderState);
}

void VoxelAssemblyScene::release_selection_meshes()
{
    if (_selectedPartBoundsMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_selectedPartBoundsMesh));
    }
    if (_selectedPartPivotMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_selectedPartPivotMesh));
    }
    if (_parentAttachmentMarkerMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_parentAttachmentMarkerMesh));
    }
    for (std::shared_ptr<Mesh>& mesh : _selectedAttachmentMarkerMeshes)
    {
        if (mesh != nullptr)
        {
            render::enqueue_mesh_release(std::move(mesh));
        }
    }
    _selectedAttachmentMarkerMeshes.clear();
}

void VoxelAssemblyScene::add_part_from_selected_model()
{
    if (_selectedSavedModelIndex < 0 || _selectedSavedModelIndex >= static_cast<int>(_savedModelAssetIds.size()))
    {
        _statusMessage = "Select a saved voxel model first";
        return;
    }

    const std::string& modelAssetId = _savedModelAssetIds[static_cast<size_t>(_selectedSavedModelIndex)];
    VoxelAssemblyPartDefinition part{};
    part.partId = make_unique_part_id(modelAssetId);
    part.displayName = modelAssetId;
    part.defaultModelAssetId = modelAssetId;
    part.visibleByDefault = true;
    const std::string addedPartId = part.partId;

    apply_assembly_edit(
        std::format("Add part '{}'", modelAssetId),
        [part = std::move(part)](VoxelAssemblyAsset& assembly) mutable
        {
            if (assembly.parts.empty())
            {
                assembly.rootPartId = part.partId;
            }

            assembly.parts.push_back(std::move(part));
        });
    if (const VoxelAssemblyPartDefinition* const addedPart = _assembly.find_part(addedPartId); addedPart != nullptr)
    {
        for (int partIndex = 0; partIndex < static_cast<int>(_assembly.parts.size()); ++partIndex)
        {
            if (_assembly.parts[static_cast<size_t>(partIndex)].partId == addedPartId)
            {
                _selectedPartIndex = partIndex;
                _selectedBindingStateIndex = 0;
                break;
            }
        }
    }
}

void VoxelAssemblyScene::remove_selected_part()
{
    if (_selectedPartIndex < 0 || _selectedPartIndex >= static_cast<int>(_assembly.parts.size()))
    {
        _statusMessage = "Select a part first";
        return;
    }

    const std::string removedPartId = _assembly.parts[static_cast<size_t>(_selectedPartIndex)].partId;
    const int removedIndex = _selectedPartIndex;
    apply_assembly_edit(
        std::format("Remove part '{}'", removedPartId),
        [removedIndex, removedPartId](VoxelAssemblyAsset& assembly)
        {
            if (removedIndex < 0 || removedIndex >= static_cast<int>(assembly.parts.size()))
            {
                return;
            }

            assembly.parts.erase(assembly.parts.begin() + removedIndex);

            if (assembly.rootPartId == removedPartId)
            {
                assembly.rootPartId = assembly.parts.empty() ? std::string{} : assembly.parts.front().partId;
            }

            for (VoxelAssemblySlotDefinition& slot : assembly.slots)
            {
                if (slot.fallbackPartId == removedPartId)
                {
                    slot.fallbackPartId.clear();
                }
            }

            for (VoxelAssemblyPartDefinition& part : assembly.parts)
            {
                for (VoxelAssemblyBindingState& state : part.bindingStates)
                {
                    if (state.parentPartId == removedPartId)
                    {
                        state.parentPartId.clear();
                        state.parentAttachmentName.clear();
                    }
                }
            }
        });
}

void VoxelAssemblyScene::mark_preview_dirty()
{
    _previewDirty = true;
    _selectionOverlayDirty = true;
}

glm::vec3 VoxelAssemblyScene::orbit_target() const
{
    return _previewOrbitTarget;
}

void VoxelAssemblyScene::apply_assembly_edit(
    const std::string_view description,
    const std::function<void(VoxelAssemblyAsset&)>& edit)
{
    if (edit == nullptr)
    {
        return;
    }

    if (!editing::apply_snapshot_edit(_history, _assembly, std::string(description), edit))
    {
        return;
    }

    sync_selection_indices();
    mark_preview_dirty();
    _statusMessage = std::format("Edited assembly: {}", description);
}

void VoxelAssemblyScene::undo_assembly_edit()
{
    if (!_history.undo(_assembly))
    {
        _statusMessage = "Nothing to undo";
        return;
    }

    sync_selection_indices();
    mark_preview_dirty();
    _statusMessage = std::format("Undo: {}", _history.redo_description());
}

void VoxelAssemblyScene::redo_assembly_edit()
{
    if (!_history.redo(_assembly))
    {
        _statusMessage = "Nothing to redo";
        return;
    }

    sync_selection_indices();
    mark_preview_dirty();
    _statusMessage = std::format("Redo: {}", _history.undo_description());
}

void VoxelAssemblyScene::sync_selection_indices()
{
    if (_selectedSlotIndex >= static_cast<int>(_assembly.slots.size()))
    {
        _selectedSlotIndex = _assembly.slots.empty() ? -1 : static_cast<int>(_assembly.slots.size()) - 1;
    }

    if (_selectedPartIndex >= static_cast<int>(_assembly.parts.size()))
    {
        _selectedPartIndex = _assembly.parts.empty() ? -1 : static_cast<int>(_assembly.parts.size()) - 1;
    }

    if (_selectedPartIndex < 0 && !_assembly.parts.empty())
    {
        _selectedPartIndex = 0;
    }

    if (VoxelAssemblyPartDefinition* const part = selected_part(); part != nullptr)
    {
        ensure_binding_state_selection(*part);
    }
    else
    {
        _selectedBindingStateIndex = 0;
    }
}

std::string VoxelAssemblyScene::make_unique_part_id(const std::string_view baseId) const
{
    std::string sanitized{};
    sanitized.reserve(baseId.size());
    for (const char ch : baseId)
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
        sanitized = "part";
    }

    if (_assembly.find_part(sanitized) == nullptr)
    {
        return sanitized;
    }

    for (int suffix = 2;; ++suffix)
    {
        const std::string candidate = std::format("{}_{}", sanitized, suffix);
        if (_assembly.find_part(candidate) == nullptr)
        {
            return candidate;
        }
    }
}

VoxelAssemblyPartDefinition* VoxelAssemblyScene::selected_part()
{
    if (_selectedPartIndex < 0 || _selectedPartIndex >= static_cast<int>(_assembly.parts.size()))
    {
        return nullptr;
    }

    return &_assembly.parts[static_cast<size_t>(_selectedPartIndex)];
}

const VoxelAssemblyPartDefinition* VoxelAssemblyScene::selected_part() const
{
    if (_selectedPartIndex < 0 || _selectedPartIndex >= static_cast<int>(_assembly.parts.size()))
    {
        return nullptr;
    }

    return &_assembly.parts[static_cast<size_t>(_selectedPartIndex)];
}

VoxelAssemblySlotDefinition* VoxelAssemblyScene::selected_slot()
{
    if (_selectedSlotIndex < 0 || _selectedSlotIndex >= static_cast<int>(_assembly.slots.size()))
    {
        return nullptr;
    }

    return &_assembly.slots[static_cast<size_t>(_selectedSlotIndex)];
}

const VoxelAssemblySlotDefinition* VoxelAssemblyScene::selected_slot() const
{
    if (_selectedSlotIndex < 0 || _selectedSlotIndex >= static_cast<int>(_assembly.slots.size()))
    {
        return nullptr;
    }

    return &_assembly.slots[static_cast<size_t>(_selectedSlotIndex)];
}

void VoxelAssemblyScene::ensure_binding_state_selection(const VoxelAssemblyPartDefinition& part)
{
    if (part.bindingStates.empty())
    {
        _selectedBindingStateIndex = 0;
        return;
    }

    _selectedBindingStateIndex = std::clamp(_selectedBindingStateIndex, 0, static_cast<int>(part.bindingStates.size()) - 1);
}

VoxelAssemblyBindingState* VoxelAssemblyScene::selected_binding_state(VoxelAssemblyPartDefinition& part)
{
    ensure_binding_state_selection(part);
    if (part.bindingStates.empty())
    {
        return nullptr;
    }

    return &part.bindingStates[static_cast<size_t>(_selectedBindingStateIndex)];
}

const VoxelAssemblyBindingState* VoxelAssemblyScene::selected_binding_state(const VoxelAssemblyPartDefinition& part) const
{
    if (part.bindingStates.empty())
    {
        return nullptr;
    }

    const int index = std::clamp(_selectedBindingStateIndex, 0, static_cast<int>(part.bindingStates.size()) - 1);
    return &part.bindingStates[static_cast<size_t>(index)];
}

const VoxelAssemblyBindingState* VoxelAssemblyScene::preview_binding_state(const VoxelAssemblyPartDefinition& part) const
{
    if (_selectedPartIndex >= 0 &&
        _selectedPartIndex < static_cast<int>(_assembly.parts.size()) &&
        &_assembly.parts[static_cast<size_t>(_selectedPartIndex)] == &part)
    {
        if (const VoxelAssemblyBindingState* const selectedState = selected_binding_state(part); selectedState != nullptr)
        {
            return selectedState;
        }
    }

    return _assembly.default_binding_state(part.partId);
}

VoxelAssemblyBindingState& VoxelAssemblyScene::add_binding_state(VoxelAssemblyPartDefinition& part)
{
    const std::string partId = part.partId;
    VoxelAssemblyBindingState state{};
    state.stateId = std::format("state_{}", part.bindingStates.size());
    state.localScale = glm::vec3(1.0f);
    if (part.partId != _assembly.rootPartId)
    {
        state.parentPartId = _assembly.rootPartId;
    }
    const std::string stateId = state.stateId;

    apply_assembly_edit(
        std::format("Add binding state '{}' to '{}'", stateId, partId),
        [partId, state = std::move(state)](VoxelAssemblyAsset& assembly) mutable
        {
            VoxelAssemblyPartDefinition* targetPart = nullptr;
            for (VoxelAssemblyPartDefinition& candidate : assembly.parts)
            {
                if (candidate.partId == partId)
                {
                    targetPart = &candidate;
                    break;
                }
            }

            if (targetPart == nullptr)
            {
                return;
            }

            targetPart->bindingStates.push_back(std::move(state));
            if (targetPart->defaultStateId.empty())
            {
                targetPart->defaultStateId = targetPart->bindingStates.back().stateId;
            }
        });

    VoxelAssemblyPartDefinition* const updatedPart = selected_part();
    if (updatedPart == nullptr)
    {
        return _assembly.parts.front().bindingStates.front();
    }

    for (int stateIndex = 0; stateIndex < static_cast<int>(updatedPart->bindingStates.size()); ++stateIndex)
    {
        if (updatedPart->bindingStates[static_cast<size_t>(stateIndex)].stateId == stateId)
        {
            _selectedBindingStateIndex = stateIndex;
            break;
        }
    }
    return updatedPart->bindingStates[static_cast<size_t>(_selectedBindingStateIndex)];
}

void VoxelAssemblyScene::nudge_selected_binding_position(const glm::vec3& delta)
{
    VoxelAssemblyPartDefinition* const part = selected_part();
    if (part == nullptr)
    {
        return;
    }

    VoxelAssemblyBindingState* const bindingState = selected_binding_state(*part);
    if (bindingState == nullptr)
    {
        return;
    }

    const std::string partId = part->partId;
    const std::string stateId = bindingState->stateId;
    const glm::vec3 nextPosition = bindingState->localPositionOffset + delta;
    apply_assembly_edit(
        std::format("Move binding state '{}.{}'", partId, stateId),
        [partId, stateId, nextPosition](VoxelAssemblyAsset& assembly)
        {
            for (VoxelAssemblyPartDefinition& candidatePart : assembly.parts)
            {
                if (candidatePart.partId != partId)
                {
                    continue;
                }

                for (VoxelAssemblyBindingState& candidateState : candidatePart.bindingStates)
                {
                    if (candidateState.stateId == stateId)
                    {
                        candidateState.localPositionOffset = nextPosition;
                        return;
                    }
                }
            }
        });
}

void VoxelAssemblyScene::rotate_selected_binding_euler_degrees(const glm::vec3& deltaDegrees)
{
    VoxelAssemblyPartDefinition* const part = selected_part();
    if (part == nullptr)
    {
        return;
    }

    VoxelAssemblyBindingState* const bindingState = selected_binding_state(*part);
    if (bindingState == nullptr)
    {
        return;
    }

    const std::string partId = part->partId;
    const std::string stateId = bindingState->stateId;
    const glm::vec3 nextDegrees = euler_degrees_from_quat(bindingState->localRotationOffset) + deltaDegrees;
    const glm::quat nextRotation = glm::quat(glm::radians(nextDegrees));
    apply_assembly_edit(
        std::format("Rotate binding state '{}.{}'", partId, stateId),
        [partId, stateId, nextRotation](VoxelAssemblyAsset& assembly)
        {
            for (VoxelAssemblyPartDefinition& candidatePart : assembly.parts)
            {
                if (candidatePart.partId != partId)
                {
                    continue;
                }

                for (VoxelAssemblyBindingState& candidateState : candidatePart.bindingStates)
                {
                    if (candidateState.stateId == stateId)
                    {
                        candidateState.localRotationOffset = nextRotation;
                        return;
                    }
                }
            }
        });
}

std::vector<std::string> VoxelAssemblyScene::collect_validation_messages()
{
    std::vector<std::string> messages{};
    std::unordered_set<std::string> partIds{};
    std::unordered_set<std::string> slotIds{};
    std::unordered_map<std::string, std::string> defaultParentByPart{};

    if (_assembly.rootPartId.empty())
    {
        messages.push_back("Assembly rootPartId is empty.");
    }
    else if (_assembly.find_part(_assembly.rootPartId) == nullptr)
    {
        messages.push_back(std::format("Root part '{}' does not exist.", _assembly.rootPartId));
    }

    for (const VoxelAssemblyPartDefinition& part : _assembly.parts)
    {
        std::unordered_set<std::string> stateIds{};

        if (part.partId.empty())
        {
            messages.push_back("A part has an empty partId.");
        }
        else if (!partIds.insert(part.partId).second)
        {
            messages.push_back(std::format("Duplicate partId '{}'.", part.partId));
        }

        if (part.defaultModelAssetId.empty())
        {
            messages.push_back(std::format("Part '{}' has an empty model asset id.", part.partId));
        }
        else if (_assetManager.load_or_get(part.defaultModelAssetId) == nullptr)
        {
            messages.push_back(std::format("Part '{}' references missing model '{}'.", part.partId, part.defaultModelAssetId));
        }

        if (!part.slotId.empty() && _assembly.find_slot(part.slotId) == nullptr)
        {
            messages.push_back(std::format("Part '{}' references missing slot '{}'.", part.partId, part.slotId));
        }

        if (!part.defaultStateId.empty() && _assembly.find_binding_state(part.partId, part.defaultStateId) == nullptr)
        {
            messages.push_back(std::format("Part '{}' default state '{}' does not exist.", part.partId, part.defaultStateId));
        }

        for (const VoxelAssemblyBindingState& bindingState : part.bindingStates)
        {
            if (bindingState.stateId.empty())
            {
                messages.push_back(std::format("Part '{}' has a binding state with an empty stateId.", part.partId));
            }
            else if (!stateIds.insert(bindingState.stateId).second)
            {
                messages.push_back(std::format("Part '{}' has duplicate stateId '{}'.", part.partId, bindingState.stateId));
            }

            if (!bindingState.parentAttachmentName.empty() && bindingState.parentPartId.empty())
            {
                messages.push_back(std::format(
                    "Part '{}' state '{}' has a parent attachment but no parent part.",
                    part.partId,
                    bindingState.stateId));
            }

            if (part.partId == _assembly.rootPartId && !bindingState.parentPartId.empty())
            {
                messages.push_back(std::format(
                    "Root part '{}' state '{}' should not declare a parent part.",
                    part.partId,
                    bindingState.stateId));
            }

            if (part.partId == _assembly.rootPartId && !bindingState.parentAttachmentName.empty())
            {
                messages.push_back(std::format(
                    "Root part '{}' state '{}' should not declare a parent attachment.",
                    part.partId,
                    bindingState.stateId));
            }

            if (!bindingState.parentPartId.empty())
            {
                if (bindingState.parentPartId == part.partId)
                {
                    messages.push_back(std::format("Part '{}' state '{}' cannot parent to itself.", part.partId, bindingState.stateId));
                }
                else if (const VoxelAssemblyPartDefinition* const parentPart = _assembly.find_part(bindingState.parentPartId);
                    parentPart == nullptr)
                {
                    messages.push_back(std::format(
                        "Part '{}' state '{}' references missing parent part '{}'.",
                        part.partId,
                        bindingState.stateId,
                        bindingState.parentPartId));
                }
                else if (!bindingState.parentAttachmentName.empty())
                {
                    const std::shared_ptr<VoxelRuntimeAsset> parentAsset = _assetManager.load_or_get(parentPart->defaultModelAssetId);
                    if (parentAsset == nullptr || parentAsset->model.find_attachment(bindingState.parentAttachmentName) == nullptr)
                    {
                        messages.push_back(std::format(
                            "Part '{}' state '{}' references missing parent attachment '{}.{}'.",
                            part.partId,
                            bindingState.stateId,
                            bindingState.parentPartId,
                            bindingState.parentAttachmentName));
                    }
                }
            }

            if (bindingState.stateId == part.defaultStateId)
            {
                defaultParentByPart.insert_or_assign(part.partId, bindingState.parentPartId);
            }
        }
    }

    if (!_assembly.rootPartId.empty())
    {
        std::unordered_map<std::string, uint8_t> visitation{};
        std::function<bool(const std::string&, std::vector<std::string>&)> visitDefaultGraph =
            [&](const std::string& partId, std::vector<std::string>& stack) -> bool
        {
            const uint8_t state = visitation[partId];
            if (state == 1)
            {
                stack.push_back(partId);
                return true;
            }
            if (state == 2)
            {
                return false;
            }

            visitation[partId] = 1;
            stack.push_back(partId);
            if (const auto parentIt = defaultParentByPart.find(partId); parentIt != defaultParentByPart.end() &&
                !parentIt->second.empty())
            {
                if (visitDefaultGraph(parentIt->second, stack))
                {
                    return true;
                }
            }

            stack.pop_back();
            visitation[partId] = 2;
            return false;
        };

        for (const VoxelAssemblyPartDefinition& part : _assembly.parts)
        {
            if (visitation[part.partId] != 0)
            {
                continue;
            }

            std::vector<std::string> stack{};
            if (visitDefaultGraph(part.partId, stack))
            {
                std::string cycle = stack.empty() ? part.partId : stack.front();
                if (stack.size() > 1)
                {
                    cycle.clear();
                    for (size_t i = 0; i < stack.size(); ++i)
                    {
                        if (i > 0)
                        {
                            cycle += " -> ";
                        }
                        cycle += stack[i];
                    }
                }
                messages.push_back(std::format("Default binding graph contains a cycle: {}", cycle));
                break;
            }
        }
    }

    for (const VoxelAssemblySlotDefinition& slot : _assembly.slots)
    {
        if (slot.slotId.empty())
        {
            messages.push_back("A slot has an empty slotId.");
        }
        else if (!slotIds.insert(slot.slotId).second)
        {
            messages.push_back(std::format("Duplicate slotId '{}'.", slot.slotId));
        }

        if (!slot.fallbackPartId.empty() && _assembly.find_part(slot.fallbackPartId) == nullptr)
        {
            messages.push_back(std::format(
                "Slot '{}' references missing fallback part '{}'.",
                slot.slotId,
                slot.fallbackPartId));
        }
    }

    return messages;
}

void VoxelAssemblyScene::reset_assembly()
{
    _assembly = VoxelAssemblyAsset{};
    _history.clear();
    _selectedSlotIndex = -1;
    _selectedPartIndex = -1;
    _selectedBindingStateIndex = 0;
    _assetManager.clear();
    mark_preview_dirty();
    _statusMessage = "New assembly";
}

void VoxelAssemblyScene::save_assembly()
{
    try
    {
        _assemblyRepository.save(_assembly);
        refresh_saved_assets();
        _selectedSavedAssemblyIndex = std::max(0, static_cast<int>(_savedAssemblyAssetIds.size()) - 1);
        _statusMessage = "Saved assembly";
    }
    catch (const std::exception& exception)
    {
        _statusMessage = exception.what();
    }
}

void VoxelAssemblyScene::load_assembly()
{
    if (_selectedSavedAssemblyIndex < 0 ||
        _selectedSavedAssemblyIndex >= static_cast<int>(_savedAssemblyAssetIds.size()))
    {
        _statusMessage = "Select a saved assembly first";
        return;
    }

    load_assembly(_savedAssemblyAssetIds[static_cast<size_t>(_selectedSavedAssemblyIndex)]);
}

void VoxelAssemblyScene::load_assembly(const std::string& assetId)
{
    if (const std::optional<VoxelAssemblyAsset> loaded = _assemblyRepository.load(assetId); loaded.has_value())
    {
        _assembly = loaded.value();
        _history.clear();
        _selectedSlotIndex = _assembly.slots.empty() ? -1 : 0;
        _selectedPartIndex = _assembly.parts.empty() ? -1 : 0;
        _selectedBindingStateIndex = 0;
        _assetManager.clear();
        mark_preview_dirty();
        _statusMessage = "Loaded assembly";
        return;
    }

    _statusMessage = "Failed to load assembly";
}

void VoxelAssemblyScene::refresh_saved_assets()
{
    _savedModelAssetIds = _modelRepository.list_asset_ids();
    _savedAssemblyAssetIds = _assemblyRepository.list_asset_ids();
    _assetManager.clear();
    mark_preview_dirty();

    if (_selectedSavedModelIndex >= static_cast<int>(_savedModelAssetIds.size()))
    {
        _selectedSavedModelIndex = _savedModelAssetIds.empty() ? -1 : 0;
    }

    if (_selectedSavedAssemblyIndex >= static_cast<int>(_savedAssemblyAssetIds.size()))
    {
        _selectedSavedAssemblyIndex = _savedAssemblyAssetIds.empty() ? -1 : 0;
    }

    if (_selectedSlotIndex >= static_cast<int>(_assembly.slots.size()))
    {
        _selectedSlotIndex = _assembly.slots.empty() ? -1 : 0;
    }
}

void VoxelAssemblyScene::draw_editor_window()
{
    ImGui::Begin("Voxel Assembly");
    ImGui::Text("Scene Switch: F1 Game | F2 Voxel Editor | F3 Voxel Assembly");

    const auto make_unique_slot_id = [this]()
    {
        if (_assembly.find_slot("slot") == nullptr)
        {
            return std::string("slot");
        }

        for (int suffix = 2;; ++suffix)
        {
            const std::string candidate = std::format("slot_{}", suffix);
            if (_assembly.find_slot(candidate) == nullptr)
            {
                return candidate;
            }
        }
    };

    const auto part_list_label = [](const VoxelAssemblyPartDefinition& part)
    {
        if (!part.displayName.empty() && part.displayName != part.partId)
        {
            return std::format("{} ({})", part.displayName, part.partId);
        }

        return part.partId;
    };

    const auto slot_list_label = [](const VoxelAssemblySlotDefinition& slot)
    {
        if (!slot.displayName.empty() && slot.displayName != slot.slotId)
        {
            return std::format("{} ({})", slot.displayName, slot.slotId);
        }

        return slot.slotId;
    };

    char assetIdBuffer[128]{};
    copy_cstr_truncating(assetIdBuffer, _assembly.assetId);
    if (ImGui::InputText("Asset Id", assetIdBuffer, IM_ARRAYSIZE(assetIdBuffer)))
    {
        const std::string nextAssetId = assetIdBuffer;
        apply_assembly_edit("Rename assembly asset id", [nextAssetId](VoxelAssemblyAsset& assembly)
        {
            assembly.assetId = nextAssetId;
        });
    }

    char displayNameBuffer[128]{};
    copy_cstr_truncating(displayNameBuffer, _assembly.displayName);
    if (ImGui::InputText("Display Name", displayNameBuffer, IM_ARRAYSIZE(displayNameBuffer)))
    {
        const std::string nextDisplayName = displayNameBuffer;
        apply_assembly_edit("Rename assembly display name", [nextDisplayName](VoxelAssemblyAsset& assembly)
        {
            assembly.displayName = nextDisplayName;
        });
    }

    const char* rootPartLabel = _assembly.rootPartId.empty() ? "<none>" : _assembly.rootPartId.c_str();
    if (ImGui::BeginCombo("Root Part", rootPartLabel))
    {
        const bool noneSelected = _assembly.rootPartId.empty();
        if (ImGui::Selectable("<none>", noneSelected))
        {
            apply_assembly_edit("Clear root part", [](VoxelAssemblyAsset& assembly)
            {
                assembly.rootPartId.clear();
            });
        }

        for (int partIndex = 0; partIndex < static_cast<int>(_assembly.parts.size()); ++partIndex)
        {
            ImGui::PushID(std::format("RootPartOption{}", partIndex).c_str());
            const VoxelAssemblyPartDefinition& part = _assembly.parts[static_cast<size_t>(partIndex)];
            const bool selected = _assembly.rootPartId == part.partId;
            if (ImGui::Selectable(part.partId.c_str(), selected))
            {
                const std::string nextRootPartId = part.partId;
                apply_assembly_edit("Change root part", [nextRootPartId](VoxelAssemblyAsset& assembly)
                {
                    assembly.rootPartId = nextRootPartId;
                });
            }
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    if (ImGui::Button("New Assembly"))
    {
        reset_assembly();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Assembly"))
    {
        save_assembly();
    }
    ImGui::SameLine();
    if (!_history.can_undo())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Undo"))
    {
        undo_assembly_edit();
    }
    if (!_history.can_undo())
    {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (!_history.can_redo())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Redo"))
    {
        redo_assembly_edit();
    }
    if (!_history.can_redo())
    {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Selected Assembly"))
    {
        load_assembly();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
    {
        refresh_saved_assets();
    }

    ImGui::SeparatorText("Preview");
    ImGui::Text("Orbit: Middle Mouse Drag");
    ImGui::Text("Zoom: Mouse Wheel");
    ImGui::Text("Parts Visible: %d", static_cast<int>(_assembly.parts.size()));
    ImGui::TextWrapped("The selected part previews the binding state chosen below. Other parts use their default state.");
    if (ImGui::Checkbox("Show Selected Bounds", &_showSelectedPartBounds))
    {
        _selectionOverlayDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Selected Pivot", &_showSelectedPartPivot))
    {
        _selectionOverlayDirty = true;
    }
    if (ImGui::Checkbox("Show Selected Attachments", &_showSelectedPartAttachments))
    {
        _selectionOverlayDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Parent Attachment", &_showParentAttachmentMarker))
    {
        _selectionOverlayDirty = true;
    }

    ImGui::SeparatorText("Saved Assemblies");
    if (ImGui::BeginChild("SavedAssembliesList", ImVec2(0.0f, 120.0f), true))
    {
        ImGui::PushID("SavedAssembliesItems");
        for (int i = 0; i < static_cast<int>(_savedAssemblyAssetIds.size()); ++i)
        {
            ImGui::PushID(i);
            const bool selected = i == _selectedSavedAssemblyIndex;
            if (ImGui::Selectable(_savedAssemblyAssetIds[static_cast<size_t>(i)].c_str(), selected))
            {
                _selectedSavedAssemblyIndex = i;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Saved Voxel Models");
    if (ImGui::BeginChild("SavedVoxelModelsList", ImVec2(0.0f, 120.0f), true))
    {
        ImGui::PushID("SavedVoxelModelItems");
        for (int i = 0; i < static_cast<int>(_savedModelAssetIds.size()); ++i)
        {
            ImGui::PushID(i);
            const bool selected = i == _selectedSavedModelIndex;
            if (ImGui::Selectable(_savedModelAssetIds[static_cast<size_t>(i)].c_str(), selected))
            {
                _selectedSavedModelIndex = i;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("Add Selected Model As Part"))
    {
        add_part_from_selected_model();
    }

    ImGui::SeparatorText("Slots");
    ImGui::TextWrapped("Slots are gameplay-facing replacement hooks. In v1, a part can optionally bind to one authored slot.");
    if (ImGui::Button("Add Slot"))
    {
        VoxelAssemblySlotDefinition slot{};
        slot.slotId = make_unique_slot_id();
        slot.displayName = slot.slotId;
        const std::string addedSlotId = slot.slotId;
        apply_assembly_edit(
            std::format("Add slot '{}'", addedSlotId),
            [slot = std::move(slot)](VoxelAssemblyAsset& assembly) mutable
            {
                assembly.slots.push_back(std::move(slot));
            });
        for (int slotIndex = 0; slotIndex < static_cast<int>(_assembly.slots.size()); ++slotIndex)
        {
            if (_assembly.slots[static_cast<size_t>(slotIndex)].slotId == addedSlotId)
            {
                _selectedSlotIndex = slotIndex;
                break;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Selected Slot"))
    {
        if (VoxelAssemblySlotDefinition* const slot = selected_slot(); slot != nullptr)
        {
            const std::string removedSlotId = slot->slotId;
            const int removedSlotIndex = _selectedSlotIndex;
            apply_assembly_edit(
                std::format("Remove slot '{}'", removedSlotId),
                [removedSlotIndex, removedSlotId](VoxelAssemblyAsset& assembly)
                {
                    if (removedSlotIndex < 0 || removedSlotIndex >= static_cast<int>(assembly.slots.size()))
                    {
                        return;
                    }

                    assembly.slots.erase(assembly.slots.begin() + removedSlotIndex);
                    for (VoxelAssemblyPartDefinition& part : assembly.parts)
                    {
                        if (part.slotId == removedSlotId)
                        {
                            part.slotId.clear();
                        }
                    }
                });
        }
    }

    if (ImGui::BeginChild("AssemblySlotsList", ImVec2(0.0f, 110.0f), true))
    {
        ImGui::PushID("SlotItems");
        for (int slotIndex = 0; slotIndex < static_cast<int>(_assembly.slots.size()); ++slotIndex)
        {
            ImGui::PushID(slotIndex);
            const VoxelAssemblySlotDefinition& slot = _assembly.slots[static_cast<size_t>(slotIndex)];
            const bool selected = slotIndex == _selectedSlotIndex;
            const std::string label = slot_list_label(slot);
            if (ImGui::Selectable(label.c_str(), selected))
            {
                _selectedSlotIndex = slotIndex;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (VoxelAssemblySlotDefinition* const slot = selected_slot(); slot != nullptr)
    {
        ImGui::PushID("SelectedSlot");
        char slotIdBuffer[128]{};
        copy_cstr_truncating(slotIdBuffer, slot->slotId);
        if (ImGui::InputText("Slot Id", slotIdBuffer, IM_ARRAYSIZE(slotIdBuffer)))
        {
            const std::string previousSlotId = slot->slotId;
            const int slotIndex = _selectedSlotIndex;
            const std::string nextSlotId = slotIdBuffer;
            apply_assembly_edit("Rename slot id", [slotIndex, previousSlotId, nextSlotId](VoxelAssemblyAsset& assembly)
            {
                if (slotIndex < 0 || slotIndex >= static_cast<int>(assembly.slots.size()))
                {
                    return;
                }

                assembly.slots[static_cast<size_t>(slotIndex)].slotId = nextSlotId;
                for (VoxelAssemblyPartDefinition& part : assembly.parts)
                {
                    if (part.slotId == previousSlotId)
                    {
                        part.slotId = nextSlotId;
                    }
                }
            });
        }

        char slotDisplayNameBuffer[128]{};
        copy_cstr_truncating(slotDisplayNameBuffer, slot->displayName);
        if (ImGui::InputText("Slot Display Name", slotDisplayNameBuffer, IM_ARRAYSIZE(slotDisplayNameBuffer)))
        {
            const int slotIndex = _selectedSlotIndex;
            const std::string nextDisplayName = slotDisplayNameBuffer;
            apply_assembly_edit("Rename slot display name", [slotIndex, nextDisplayName](VoxelAssemblyAsset& assembly)
            {
                if (slotIndex >= 0 && slotIndex < static_cast<int>(assembly.slots.size()))
                {
                    assembly.slots[static_cast<size_t>(slotIndex)].displayName = nextDisplayName;
                }
            });
        }

        const char* fallbackPartLabel = slot->fallbackPartId.empty() ? "<none>" : slot->fallbackPartId.c_str();
        if (ImGui::BeginCombo("Fallback Part", fallbackPartLabel))
        {
            const bool noneSelected = slot->fallbackPartId.empty();
            if (ImGui::Selectable("<none>", noneSelected))
            {
                const int slotIndex = _selectedSlotIndex;
                apply_assembly_edit("Clear slot fallback part", [slotIndex](VoxelAssemblyAsset& assembly)
                {
                    if (slotIndex >= 0 && slotIndex < static_cast<int>(assembly.slots.size()))
                    {
                        assembly.slots[static_cast<size_t>(slotIndex)].fallbackPartId.clear();
                    }
                });
            }

            for (int partIndex = 0; partIndex < static_cast<int>(_assembly.parts.size()); ++partIndex)
            {
                ImGui::PushID(std::format("SlotFallbackPart{}", partIndex).c_str());
                const VoxelAssemblyPartDefinition& part = _assembly.parts[static_cast<size_t>(partIndex)];
                const bool selected = slot->fallbackPartId == part.partId;
                if (ImGui::Selectable(part.partId.c_str(), selected))
                {
                    const int slotIndex = _selectedSlotIndex;
                    const std::string nextFallbackPartId = part.partId;
                    apply_assembly_edit("Set slot fallback part", [slotIndex, nextFallbackPartId](VoxelAssemblyAsset& assembly)
                    {
                        if (slotIndex >= 0 && slotIndex < static_cast<int>(assembly.slots.size()))
                        {
                            assembly.slots[static_cast<size_t>(slotIndex)].fallbackPartId = nextFallbackPartId;
                        }
                    });
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        bool required = slot->required;
        if (ImGui::Checkbox("Required Slot", &required))
        {
            const int slotIndex = _selectedSlotIndex;
            apply_assembly_edit("Toggle required slot", [slotIndex, required](VoxelAssemblyAsset& assembly)
            {
                if (slotIndex >= 0 && slotIndex < static_cast<int>(assembly.slots.size()))
                {
                    assembly.slots[static_cast<size_t>(slotIndex)].required = required;
                }
            });
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Assembly Parts");
    ImGui::TextWrapped("Parts are logical nodes in the assembly graph. Each part references one voxel model and can optionally bind to one slot.");
    if (ImGui::BeginChild("AssemblyPartsList", ImVec2(0.0f, 140.0f), true))
    {
        ImGui::PushID("AssemblyPartItems");
        for (int i = 0; i < static_cast<int>(_assembly.parts.size()); ++i)
        {
            ImGui::PushID(i);
            const VoxelAssemblyPartDefinition& part = _assembly.parts[static_cast<size_t>(i)];
            const bool selected = i == _selectedPartIndex;
            const std::string label = part_list_label(part);
            if (ImGui::Selectable(label.c_str(), selected))
            {
                _selectedPartIndex = i;
                _selectedBindingStateIndex = 0;
                mark_preview_dirty();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("Remove Selected Part"))
    {
        remove_selected_part();
    }

    if (_selectedPartIndex >= 0 && _selectedPartIndex < static_cast<int>(_assembly.parts.size()))
    {
        VoxelAssemblyPartDefinition& part = _assembly.parts[static_cast<size_t>(_selectedPartIndex)];
        const bool isRoot = part.partId == _assembly.rootPartId;
        ImGui::SeparatorText("Selected Part");
        ImGui::TextWrapped("Attachment = named mount point on a voxel model. Binding state = one mount configuration for this part.");

        char partIdBuffer[128]{};
        copy_cstr_truncating(partIdBuffer, part.partId);
        if (ImGui::InputText("Part Id", partIdBuffer, IM_ARRAYSIZE(partIdBuffer)))
        {
            const std::string previousPartId = part.partId;
            const int partIndex = _selectedPartIndex;
            const std::string nextPartId = partIdBuffer;
            apply_assembly_edit("Rename part id", [partIndex, previousPartId, nextPartId](VoxelAssemblyAsset& assembly)
            {
                if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                {
                    return;
                }

                assembly.parts[static_cast<size_t>(partIndex)].partId = nextPartId;

                if (assembly.rootPartId == previousPartId)
                {
                    assembly.rootPartId = nextPartId;
                }

                for (VoxelAssemblySlotDefinition& slot : assembly.slots)
                {
                    if (slot.fallbackPartId == previousPartId)
                    {
                        slot.fallbackPartId = nextPartId;
                    }
                }

                for (VoxelAssemblyPartDefinition& otherPart : assembly.parts)
                {
                    for (VoxelAssemblyBindingState& state : otherPart.bindingStates)
                    {
                        if (state.parentPartId == previousPartId)
                        {
                            state.parentPartId = nextPartId;
                        }
                    }
                }
            });
        }

        char partDisplayNameBuffer[128]{};
        copy_cstr_truncating(partDisplayNameBuffer, part.displayName);
        if (ImGui::InputText("Part Display Name", partDisplayNameBuffer, IM_ARRAYSIZE(partDisplayNameBuffer)))
        {
            const int partIndex = _selectedPartIndex;
            const std::string nextDisplayName = partDisplayNameBuffer;
            apply_assembly_edit("Rename part display name", [partIndex, nextDisplayName](VoxelAssemblyAsset& assembly)
            {
                if (partIndex >= 0 && partIndex < static_cast<int>(assembly.parts.size()))
                {
                    assembly.parts[static_cast<size_t>(partIndex)].displayName = nextDisplayName;
                }
            });
        }

        char partModelAssetBuffer[128]{};
        copy_cstr_truncating(partModelAssetBuffer, part.defaultModelAssetId);
        if (ImGui::InputText("Model Asset Id", partModelAssetBuffer, IM_ARRAYSIZE(partModelAssetBuffer)))
        {
            const int partIndex = _selectedPartIndex;
            const std::string nextModelAssetId = partModelAssetBuffer;
            apply_assembly_edit("Change part model asset", [partIndex, nextModelAssetId](VoxelAssemblyAsset& assembly)
            {
                if (partIndex >= 0 && partIndex < static_cast<int>(assembly.parts.size()))
                {
                    assembly.parts[static_cast<size_t>(partIndex)].defaultModelAssetId = nextModelAssetId;
                }
            });
        }
        if (_selectedSavedModelIndex >= 0 && _selectedSavedModelIndex < static_cast<int>(_savedModelAssetIds.size()))
        {
            ImGui::SameLine();
            if (ImGui::Button("Use Selected Saved Model"))
            {
                const int partIndex = _selectedPartIndex;
                const std::string nextModelAssetId = _savedModelAssetIds[static_cast<size_t>(_selectedSavedModelIndex)];
                apply_assembly_edit("Use saved model for part", [partIndex, nextModelAssetId](VoxelAssemblyAsset& assembly)
                {
                    if (partIndex >= 0 && partIndex < static_cast<int>(assembly.parts.size()))
                    {
                        assembly.parts[static_cast<size_t>(partIndex)].defaultModelAssetId = nextModelAssetId;
                    }
                });
            }
        }

        bool visibleByDefault = part.visibleByDefault;
        if (ImGui::Checkbox("Visible By Default", &visibleByDefault))
        {
            const int partIndex = _selectedPartIndex;
            apply_assembly_edit("Toggle part default visibility", [partIndex, visibleByDefault](VoxelAssemblyAsset& assembly)
            {
                if (partIndex >= 0 && partIndex < static_cast<int>(assembly.parts.size()))
                {
                    assembly.parts[static_cast<size_t>(partIndex)].visibleByDefault = visibleByDefault;
                }
            });
        }

        const char* slotBindingLabel = part.slotId.empty() ? "<none>" : part.slotId.c_str();
        if (ImGui::BeginCombo("Slot Binding", slotBindingLabel))
        {
            const bool noneSelected = part.slotId.empty();
            if (ImGui::Selectable("<none>", noneSelected))
            {
                const int partIndex = _selectedPartIndex;
                apply_assembly_edit("Clear part slot binding", [partIndex](VoxelAssemblyAsset& assembly)
                {
                    if (partIndex >= 0 && partIndex < static_cast<int>(assembly.parts.size()))
                    {
                        assembly.parts[static_cast<size_t>(partIndex)].slotId.clear();
                    }
                });
            }

            for (int slotIndex = 0; slotIndex < static_cast<int>(_assembly.slots.size()); ++slotIndex)
            {
                ImGui::PushID(std::format("PartSlotOption{}", slotIndex).c_str());
                const VoxelAssemblySlotDefinition& slot = _assembly.slots[static_cast<size_t>(slotIndex)];
                const bool selected = part.slotId == slot.slotId;
                if (ImGui::Selectable(slot.slotId.c_str(), selected))
                {
                    const int partIndex = _selectedPartIndex;
                    const std::string nextSlotId = slot.slotId;
                    apply_assembly_edit("Set part slot binding", [partIndex, nextSlotId](VoxelAssemblyAsset& assembly)
                    {
                        if (partIndex >= 0 && partIndex < static_cast<int>(assembly.parts.size()))
                        {
                            assembly.parts[static_cast<size_t>(partIndex)].slotId = nextSlotId;
                        }
                    });
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Slots let runtime systems replace the part's model by gameplay-facing id.");

        if (!isRoot && ImGui::Button("Make Root"))
        {
            const std::string nextRootPartId = part.partId;
            apply_assembly_edit("Make part root", [nextRootPartId](VoxelAssemblyAsset& assembly)
            {
                assembly.rootPartId = nextRootPartId;
            });
        }
        else if (isRoot)
        {
            ImGui::TextDisabled("This is the assembly root part.");
        }

        ImGui::SeparatorText("Binding States");
        ImGui::TextWrapped("The selected state is what this editor previews for the selected part. Runtime uses the default state when nothing overrides it.");
        ensure_binding_state_selection(part);

        if (part.bindingStates.empty())
        {
            if (ImGui::Button("Add First Binding State"))
            {
                add_binding_state(part);
                const int partIndex = _selectedPartIndex;
                const int stateIndex = _selectedBindingStateIndex;
                apply_assembly_edit("Initialize default binding state", [partIndex, stateIndex](VoxelAssemblyAsset& assembly)
                {
                    if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                    {
                        return;
                    }

                    VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                    if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                    {
                        return;
                    }

                    editedPart.bindingStates[static_cast<size_t>(stateIndex)].stateId = "default";
                    editedPart.defaultStateId = "default";
                });
            }
        }
        else
        {
            const VoxelAssemblyBindingState* const currentState = selected_binding_state(part);
            const char* currentStateLabel = currentState != nullptr ? currentState->stateId.c_str() : "<none>";
            if (ImGui::BeginCombo("Edit / Preview State", currentStateLabel))
            {
                for (int stateIndex = 0; stateIndex < static_cast<int>(part.bindingStates.size()); ++stateIndex)
                {
                    ImGui::PushID(stateIndex);
                    const bool selected = stateIndex == _selectedBindingStateIndex;
                    if (ImGui::Selectable(part.bindingStates[static_cast<size_t>(stateIndex)].stateId.c_str(), selected))
                    {
                        _selectedBindingStateIndex = stateIndex;
                        mark_preview_dirty();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Add Binding State"))
            {
                add_binding_state(part);
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Binding State"))
            {
                if (!part.bindingStates.empty())
                {
                    const std::string removedStateId = part.bindingStates[static_cast<size_t>(_selectedBindingStateIndex)].stateId;
                    const int partIndex = _selectedPartIndex;
                    const int removedStateIndex = _selectedBindingStateIndex;
                    apply_assembly_edit(
                        std::format("Remove binding state '{}'", removedStateId),
                        [partIndex, removedStateIndex, removedStateId](VoxelAssemblyAsset& assembly)
                        {
                            if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                            {
                                return;
                            }

                            VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                            if (removedStateIndex < 0 || removedStateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                            {
                                return;
                            }

                            editedPart.bindingStates.erase(editedPart.bindingStates.begin() + removedStateIndex);
                            if (editedPart.defaultStateId == removedStateId)
                            {
                                editedPart.defaultStateId = editedPart.bindingStates.empty()
                                    ? std::string{}
                                    : editedPart.bindingStates.front().stateId;
                            }
                        });
                }
            }

            const VoxelAssemblyBindingState* const defaultState = _assembly.default_binding_state(part.partId);
            const char* defaultStateLabel = defaultState != nullptr ? defaultState->stateId.c_str() : "<none>";
            if (ImGui::BeginCombo("Default State", defaultStateLabel))
            {
                for (int stateIndex = 0; stateIndex < static_cast<int>(part.bindingStates.size()); ++stateIndex)
                {
                    ImGui::PushID(1000 + stateIndex);
                    const std::string& stateId = part.bindingStates[static_cast<size_t>(stateIndex)].stateId;
                    const bool selected = part.defaultStateId == stateId;
                    if (ImGui::Selectable(stateId.c_str(), selected))
                    {
                        const int partIndex = _selectedPartIndex;
                        const std::string nextDefaultStateId = stateId;
                        apply_assembly_edit("Set default binding state", [partIndex, nextDefaultStateId](VoxelAssemblyAsset& assembly)
                        {
                            if (partIndex >= 0 && partIndex < static_cast<int>(assembly.parts.size()))
                            {
                                assembly.parts[static_cast<size_t>(partIndex)].defaultStateId = nextDefaultStateId;
                            }
                        });
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            if (VoxelAssemblyBindingState* const bindingState = selected_binding_state(part); bindingState != nullptr)
            {
                char stateIdBuffer[128]{};
                copy_cstr_truncating(stateIdBuffer, bindingState->stateId);
                if (ImGui::InputText("State Id", stateIdBuffer, IM_ARRAYSIZE(stateIdBuffer)))
                {
                    const std::string previousStateId = bindingState->stateId;
                    const int partIndex = _selectedPartIndex;
                    const int stateIndex = _selectedBindingStateIndex;
                    const std::string nextStateId = stateIdBuffer;
                    apply_assembly_edit("Rename binding state", [partIndex, stateIndex, previousStateId, nextStateId](VoxelAssemblyAsset& assembly)
                    {
                        if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                        {
                            return;
                        }

                        VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                        if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                        {
                            return;
                        }

                        editedPart.bindingStates[static_cast<size_t>(stateIndex)].stateId = nextStateId;
                        if (editedPart.defaultStateId == previousStateId)
                        {
                            editedPart.defaultStateId = nextStateId;
                        }
                    });
                }

                if (isRoot)
                {
                    ImGui::TextDisabled("Root states define assembly-space position, rotation, scale, and visibility.");
                    ImGui::TextDisabled("Parent fields are ignored for the root.");
                }
                else
                {
                    int parentPartChoice = 0;
                    std::vector<std::string> parentPartIds{};
                    std::vector<std::string> parentPartLabelStorage{};
                    std::vector<const char*> parentPartLabels{};
                    parentPartIds.emplace_back();
                    parentPartLabelStorage.emplace_back("<none>");
                    for (const VoxelAssemblyPartDefinition& candidate : _assembly.parts)
                    {
                        if (candidate.partId == part.partId)
                        {
                            continue;
                        }
                        parentPartIds.push_back(candidate.partId);
                        parentPartLabelStorage.push_back(candidate.partId);
                    }
                    parentPartLabels.reserve(parentPartLabelStorage.size());
                    for (const std::string& label : parentPartLabelStorage)
                    {
                        parentPartLabels.push_back(label.c_str());
                    }
                    for (int optionIndex = 0; optionIndex < static_cast<int>(parentPartIds.size()); ++optionIndex)
                    {
                        if (parentPartIds[static_cast<size_t>(optionIndex)] == bindingState->parentPartId)
                        {
                            parentPartChoice = optionIndex;
                            break;
                        }
                    }
                    if (ImGui::Combo("Parent Part", &parentPartChoice, parentPartLabels.data(), static_cast<int>(parentPartLabels.size())))
                    {
                        const int partIndex = _selectedPartIndex;
                        const int stateIndex = _selectedBindingStateIndex;
                        const std::string nextParentPartId = parentPartIds[static_cast<size_t>(parentPartChoice)];
                        apply_assembly_edit("Set binding parent part", [partIndex, stateIndex, nextParentPartId](VoxelAssemblyAsset& assembly)
                        {
                            if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                            {
                                return;
                            }

                            VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                            if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                            {
                                return;
                            }

                            editedPart.bindingStates[static_cast<size_t>(stateIndex)].parentPartId = nextParentPartId;
                            editedPart.bindingStates[static_cast<size_t>(stateIndex)].parentAttachmentName.clear();
                        });
                    }

                    std::vector<std::string> parentAttachmentNames{};
                    std::shared_ptr<VoxelRuntimeAsset> parentAsset{};
                    const VoxelAssemblyPartDefinition* parentPart = nullptr;
                    if (!bindingState->parentPartId.empty())
                    {
                        parentPart = _assembly.find_part(bindingState->parentPartId);
                        if (parentPart != nullptr)
                        {
                            parentAsset = _assetManager.load_or_get(parentPart->defaultModelAssetId);
                            if (parentAsset != nullptr)
                            {
                                for (const VoxelAttachment& attachment : parentAsset->model.attachments)
                                {
                                    parentAttachmentNames.push_back(attachment.name);
                                }
                            }
                        }
                    }

                    ImGui::TextDisabled("Parent Attachment (Optional) chooses a named mount point on the selected parent model.");

                    if (bindingState->parentPartId.empty())
                    {
                        ImGui::BeginDisabled();
                        const char* const noParentLabel = "<set parent part first>";
                        if (ImGui::BeginCombo("Parent Attachment (Optional)", noParentLabel))
                        {
                            ImGui::EndCombo();
                        }
                        ImGui::EndDisabled();
                        ImGui::TextDisabled("Set Parent Part first. Leaving attachment empty binds relative to the parent pivot.");
                    }
                    else if (parentPart == nullptr || parentAsset == nullptr)
                    {
                        ImGui::BeginDisabled();
                        const char* const missingParentLabel = "<parent model unavailable>";
                        if (ImGui::BeginCombo("Parent Attachment (Optional)", missingParentLabel))
                        {
                            ImGui::EndCombo();
                        }
                        ImGui::EndDisabled();
                        ImGui::TextDisabled("The selected parent model could not be loaded, so authored attachments are unavailable.");
                    }
                    else if (!parentAttachmentNames.empty())
                    {
                        const bool hasValidSelection =
                            bindingState->parentAttachmentName.empty() ||
                            std::ranges::find(parentAttachmentNames, bindingState->parentAttachmentName) != parentAttachmentNames.end();
                        const std::string currentAttachmentLabel = bindingState->parentAttachmentName.empty()
                            ? std::string("<parent pivot>")
                            : (hasValidSelection
                                ? bindingState->parentAttachmentName
                                : std::format("<invalid: {}>", bindingState->parentAttachmentName));
                        if (ImGui::BeginCombo("Parent Attachment (Optional)", currentAttachmentLabel.c_str()))
                        {
                            const bool pivotSelected = bindingState->parentAttachmentName.empty();
                            if (ImGui::Selectable("<parent pivot>", pivotSelected))
                            {
                                const int partIndex = _selectedPartIndex;
                                const int stateIndex = _selectedBindingStateIndex;
                                apply_assembly_edit("Clear parent attachment", [partIndex, stateIndex](VoxelAssemblyAsset& assembly)
                                {
                                    if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                                    {
                                        return;
                                    }

                                    VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                                    if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                                    {
                                        return;
                                    }

                                    editedPart.bindingStates[static_cast<size_t>(stateIndex)].parentAttachmentName.clear();
                                });
                            }

                            for (int attachmentIndex = 0; attachmentIndex < static_cast<int>(parentAttachmentNames.size()); ++attachmentIndex)
                            {
                                ImGui::PushID(2000 + attachmentIndex);
                                const std::string& attachmentName = parentAttachmentNames[static_cast<size_t>(attachmentIndex)];
                                const bool selected = bindingState->parentAttachmentName == attachmentName;
                                if (ImGui::Selectable(attachmentName.c_str(), selected))
                                {
                                    const int partIndex = _selectedPartIndex;
                                    const int stateIndex = _selectedBindingStateIndex;
                                    apply_assembly_edit("Set parent attachment", [partIndex, stateIndex, attachmentName](VoxelAssemblyAsset& assembly)
                                    {
                                        if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                                        {
                                            return;
                                        }

                                        VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                                        if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                                        {
                                            return;
                                        }

                                        editedPart.bindingStates[static_cast<size_t>(stateIndex)].parentAttachmentName = attachmentName;
                                    });
                                }
                                ImGui::PopID();
                            }

                            ImGui::EndCombo();
                        }

                        std::string availableAttachments{};
                        for (size_t attachmentIndex = 0; attachmentIndex < parentAttachmentNames.size(); ++attachmentIndex)
                        {
                            if (attachmentIndex > 0)
                            {
                                availableAttachments += ", ";
                            }
                            availableAttachments += parentAttachmentNames[attachmentIndex];
                        }
                        ImGui::TextWrapped("Available Parent Attachments: %s", availableAttachments.c_str());
                        ImGui::TextDisabled("Choose <parent pivot> to position the child relative to the parent pivot/origin.");

                        if (!hasValidSelection && !bindingState->parentAttachmentName.empty())
                        {
                            ImGui::TextColored(
                                ImVec4(1.0f, 0.72f, 0.38f, 1.0f),
                                "Current attachment '%s' does not exist on parent '%s'.",
                                bindingState->parentAttachmentName.c_str(),
                                bindingState->parentPartId.c_str());
                            if (ImGui::Button("Clear Invalid Attachment"))
                            {
                                const int partIndex = _selectedPartIndex;
                                const int stateIndex = _selectedBindingStateIndex;
                                apply_assembly_edit("Clear invalid parent attachment", [partIndex, stateIndex](VoxelAssemblyAsset& assembly)
                                {
                                    if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                                    {
                                        return;
                                    }

                                    VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                                    if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                                    {
                                        return;
                                    }

                                    editedPart.bindingStates[static_cast<size_t>(stateIndex)].parentAttachmentName.clear();
                                });
                            }
                        }
                    }
                    else
                    {
                        ImGui::BeginDisabled();
                        const char* const pivotOnlyLabel = "<parent pivot>";
                        if (ImGui::BeginCombo("Parent Attachment (Optional)", pivotOnlyLabel))
                        {
                            ImGui::EndCombo();
                        }
                        ImGui::EndDisabled();
                        ImGui::TextDisabled("No attachments are authored on the selected parent model. This binding uses the parent pivot.");

                        if (!bindingState->parentAttachmentName.empty())
                        {
                            ImGui::TextColored(
                                ImVec4(1.0f, 0.72f, 0.38f, 1.0f),
                                "Current attachment '%s' is invalid because parent '%s' has no authored attachments.",
                                bindingState->parentAttachmentName.c_str(),
                                bindingState->parentPartId.c_str());
                            if (ImGui::Button("Clear Invalid Attachment"))
                            {
                                const int partIndex = _selectedPartIndex;
                                const int stateIndex = _selectedBindingStateIndex;
                                apply_assembly_edit("Clear invalid parent attachment", [partIndex, stateIndex](VoxelAssemblyAsset& assembly)
                                {
                                    if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                                    {
                                        return;
                                    }

                                    VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                                    if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                                    {
                                        return;
                                    }

                                    editedPart.bindingStates[static_cast<size_t>(stateIndex)].parentAttachmentName.clear();
                                });
                            }
                        }
                    }
                }

                glm::vec3 localPosition = bindingState->localPositionOffset;
                if (ImGui::InputFloat3("Local Position", &localPosition.x, "%.3f"))
                {
                    const int partIndex = _selectedPartIndex;
                    const int stateIndex = _selectedBindingStateIndex;
                    const glm::vec3 nextPosition = localPosition;
                    apply_assembly_edit("Edit binding local position", [partIndex, stateIndex, nextPosition](VoxelAssemblyAsset& assembly)
                    {
                        if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                        {
                            return;
                        }

                        VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                        if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                        {
                            return;
                        }

                        editedPart.bindingStates[static_cast<size_t>(stateIndex)].localPositionOffset = nextPosition;
                    });
                }

                const std::shared_ptr<VoxelRuntimeAsset> selectedAsset = _assetManager.load_or_get(part.defaultModelAssetId);
                const float nudgeStep = std::max(selectedAsset != nullptr ? selectedAsset->model.voxelSize : (1.0f / 16.0f), 0.001f);
                ImGui::Text("Nudge Step: %.4f", nudgeStep);
                if (ImGui::Button("-X##NudgePos"))
                {
                    nudge_selected_binding_position(glm::vec3(-nudgeStep, 0.0f, 0.0f));
                }
                ImGui::SameLine();
                if (ImGui::Button("+X##NudgePos"))
                {
                    nudge_selected_binding_position(glm::vec3(nudgeStep, 0.0f, 0.0f));
                }
                ImGui::SameLine();
                if (ImGui::Button("-Y##NudgePos"))
                {
                    nudge_selected_binding_position(glm::vec3(0.0f, -nudgeStep, 0.0f));
                }
                ImGui::SameLine();
                if (ImGui::Button("+Y##NudgePos"))
                {
                    nudge_selected_binding_position(glm::vec3(0.0f, nudgeStep, 0.0f));
                }
                ImGui::SameLine();
                if (ImGui::Button("-Z##NudgePos"))
                {
                    nudge_selected_binding_position(glm::vec3(0.0f, 0.0f, -nudgeStep));
                }
                ImGui::SameLine();
                if (ImGui::Button("+Z##NudgePos"))
                {
                    nudge_selected_binding_position(glm::vec3(0.0f, 0.0f, nudgeStep));
                }

                glm::vec3 localRotationDegrees = euler_degrees_from_quat(bindingState->localRotationOffset);
                if (ImGui::InputFloat3("Local Rotation (Deg)", &localRotationDegrees.x, "%.1f"))
                {
                    const int partIndex = _selectedPartIndex;
                    const int stateIndex = _selectedBindingStateIndex;
                    const glm::quat nextRotation = glm::quat(glm::radians(localRotationDegrees));
                    apply_assembly_edit("Edit binding local rotation", [partIndex, stateIndex, nextRotation](VoxelAssemblyAsset& assembly)
                    {
                        if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                        {
                            return;
                        }

                        VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                        if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                        {
                            return;
                        }

                        editedPart.bindingStates[static_cast<size_t>(stateIndex)].localRotationOffset = nextRotation;
                    });
                }
                if (ImGui::Button("-90 X##Rotate"))
                {
                    rotate_selected_binding_euler_degrees(glm::vec3(-90.0f, 0.0f, 0.0f));
                }
                ImGui::SameLine();
                if (ImGui::Button("+90 X##Rotate"))
                {
                    rotate_selected_binding_euler_degrees(glm::vec3(90.0f, 0.0f, 0.0f));
                }
                ImGui::SameLine();
                if (ImGui::Button("-90 Y##Rotate"))
                {
                    rotate_selected_binding_euler_degrees(glm::vec3(0.0f, -90.0f, 0.0f));
                }
                ImGui::SameLine();
                if (ImGui::Button("+90 Y##Rotate"))
                {
                    rotate_selected_binding_euler_degrees(glm::vec3(0.0f, 90.0f, 0.0f));
                }
                ImGui::SameLine();
                if (ImGui::Button("-90 Z##Rotate"))
                {
                    rotate_selected_binding_euler_degrees(glm::vec3(0.0f, 0.0f, -90.0f));
                }
                ImGui::SameLine();
                if (ImGui::Button("+90 Z##Rotate"))
                {
                    rotate_selected_binding_euler_degrees(glm::vec3(0.0f, 0.0f, 90.0f));
                }

                glm::vec3 localScale = bindingState->localScale;
                if (ImGui::InputFloat3("Local Scale", &localScale.x, "%.3f"))
                {
                    const int partIndex = _selectedPartIndex;
                    const int stateIndex = _selectedBindingStateIndex;
                    const glm::vec3 nextScale = glm::max(localScale, glm::vec3(0.001f));
                    apply_assembly_edit("Edit binding local scale", [partIndex, stateIndex, nextScale](VoxelAssemblyAsset& assembly)
                    {
                        if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                        {
                            return;
                        }

                        VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                        if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                        {
                            return;
                        }

                        editedPart.bindingStates[static_cast<size_t>(stateIndex)].localScale = nextScale;
                    });
                }
                bool stateVisible = bindingState->visible;
                if (ImGui::Checkbox("State Visible", &stateVisible))
                {
                    const int partIndex = _selectedPartIndex;
                    const int stateIndex = _selectedBindingStateIndex;
                    apply_assembly_edit("Toggle binding state visibility", [partIndex, stateIndex, stateVisible](VoxelAssemblyAsset& assembly)
                    {
                        if (partIndex < 0 || partIndex >= static_cast<int>(assembly.parts.size()))
                        {
                            return;
                        }

                        VoxelAssemblyPartDefinition& editedPart = assembly.parts[static_cast<size_t>(partIndex)];
                        if (stateIndex < 0 || stateIndex >= static_cast<int>(editedPart.bindingStates.size()))
                        {
                            return;
                        }

                        editedPart.bindingStates[static_cast<size_t>(stateIndex)].visible = stateVisible;
                    });
                }
            }
        }
    }

    ImGui::SeparatorText("Current Assembly");
    ImGui::Text("Root Part: %s", _assembly.rootPartId.empty() ? "<none>" : _assembly.rootPartId.c_str());
    ImGui::Text("Part Count: %d", static_cast<int>(_assembly.parts.size()));
    ImGui::Text("Slot Count: %d", static_cast<int>(_assembly.slots.size()));
    ImGui::TextUnformatted("Phase 3: parent selection, attachment selection, binding editing, slot authoring, selection overlays, and validation are active.");

    ImGui::SeparatorText("Validation");
    const std::vector<std::string> validationMessages = collect_validation_messages();
    if (validationMessages.empty())
    {
        ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f), "No validation issues.");
    }
    else
    {
        for (int messageIndex = 0; messageIndex < static_cast<int>(validationMessages.size()); ++messageIndex)
        {
            ImGui::PushID(3000 + messageIndex);
            ImGui::TextWrapped("- %s", validationMessages[static_cast<size_t>(messageIndex)].c_str());
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", _statusMessage.c_str());

    ImGui::End();
}
