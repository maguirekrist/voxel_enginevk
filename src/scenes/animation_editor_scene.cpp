#include "animation_editor_scene.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <ranges>

#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <SDL.h>

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "orbit_orientation_gizmo.h"
#include "render/material_manager.h"
#include "render/mesh_manager.h"
#include "voxel/voxel_animation_runtime.h"
#include "voxel/voxel_assembly_component_adapter.h"
#include "voxel/voxel_picking.h"
#include "voxel/voxel_spatial_bounds.h"
#include "vk_util.h"

namespace
{
    constexpr std::string_view AnimationEditorMaterialScope = "animation_editor";
    constexpr glm::vec3 EditorBackgroundColor{0.07f, 0.08f, 0.10f};
    constexpr glm::vec3 EditorFogColor{0.13f, 0.15f, 0.18f};

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

AnimationEditorScene::AnimationEditorScene(const SceneServices& services) :
    _services(services),
    _modelRepository(_documentStore),
    _assemblyRepository(_documentStore),
    _clipRepository(_documentStore),
    _controllerRepository(_documentStore),
    _assetManager(_modelRepository),
    _assemblyAssetManager(_assemblyRepository),
    _clipAssetManager(_clipRepository),
    _controllerAssetManager(_controllerRepository)
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
    _previewAssembly.position = glm::vec3(0.0f);
    _previewAssembly.placementPolicy = VoxelPlacementPolicy::BottomCenter;
    _previewAssembly.visible = true;

    new_clip();
    new_controller();
    refresh_asset_lists();
    build_pipelines();
    update_camera();
    update_uniform_buffers();
}

AnimationEditorScene::~AnimationEditorScene()
{
    _previewRegistry.clear(_renderState);
}

void AnimationEditorScene::update_buffers()
{
    sync_preview();
    _previewRegistry.sync(*_services.meshManager, *_services.materialManager, AnimationEditorMaterialScope, _renderState);
    update_uniform_buffers();
}

void AnimationEditorScene::update(const float deltaTime)
{
    if (_playing)
    {
        _previewTimeSeconds += deltaTime;
        _previewDirty = true;
    }

    update_camera();
}

void AnimationEditorScene::handle_input(const SDL_Event& event)
{
    if (USE_IMGUI && ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    if (event.type == SDL_MOUSEMOTION && _orbitDragging)
    {
        _orbitYawDegrees += static_cast<float>(event.motion.xrel) * 0.18f;
        _orbitPitchDegrees -= static_cast<float>(event.motion.yrel) * 0.18f;
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
        return;
    }

    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_MIDDLE)
    {
        _orbitDragging = false;
        return;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        pick_part(event.button.x, event.button.y);
    }
}

void AnimationEditorScene::handle_keystate(const Uint8* state)
{
    (void)state;
}

void AnimationEditorScene::clear_input()
{
    _orbitDragging = false;
}

void AnimationEditorScene::draw_imgui()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    draw_editor_window();
    draw_orientation_gizmo();
    ImGui::Render();
}

void AnimationEditorScene::build_pipelines()
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
        AnimationEditorMaterialScope,
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource)
        },
        { translate },
        {},
        "tri_mesh.vert.spv",
        "tri_mesh.frag.spv",
        "defaultmesh");

    _services.materialManager->build_postprocess_pipeline(AnimationEditorMaterialScope, _fogResource);
    _services.materialManager->build_present_pipeline(AnimationEditorMaterialScope);
}

void AnimationEditorScene::rebuild_pipelines()
{
    _camera->resize(_services.current_window_extent());
    build_pipelines();
}

SceneRenderState& AnimationEditorScene::get_render_state()
{
    return _renderState;
}

void AnimationEditorScene::update_uniform_buffers() const
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

void AnimationEditorScene::update_camera()
{
    _orbitPitchDegrees = std::clamp(_orbitPitchDegrees, -85.0f, 85.0f);
    _orbitDistance = std::clamp(_orbitDistance, 0.75f, 64.0f);

    const glm::vec3 front = orbit_front(_orbitYawDegrees, _orbitPitchDegrees);
    _camera->_front = front;
    _camera->_up = glm::vec3(0.0f, 1.0f, 0.0f);
    _camera->_position = orbit_target() - (front * _orbitDistance);
    _camera->update(0.0f);
}

void AnimationEditorScene::draw_orientation_gizmo()
{
    glm::mat4 gizmoView = _camera->_view;
    if (draw_orbit_orientation_gizmo(gizmoView, _orbitDistance))
    {
        const glm::mat4 inverseView = glm::inverse(gizmoView);
        const glm::vec3 target = orbit_target();
        const glm::vec3 position = glm::vec3(inverseView[3]);
        const glm::vec3 front = glm::normalize(target - position);
        _orbitDistance = glm::distance(position, target);
        _orbitYawDegrees = glm::degrees(std::atan2(front.z, front.x));
        _orbitPitchDegrees = glm::degrees(std::asin(std::clamp(front.y, -1.0f, 1.0f)));
    }
}

glm::vec3 AnimationEditorScene::orbit_target() const
{
    return _previewOrbitTarget;
}

void AnimationEditorScene::pick_part(const int mouseX, const int mouseY)
{
    if (_previewParts.empty())
    {
        return;
    }

    const voxel::picking::Ray ray = voxel::picking::build_ray_from_cursor(
        mouseX,
        mouseY,
        _services.current_window_extent(),
        _camera->_position,
        glm::inverse(_camera->_projection * _camera->_view));

    float bestDistance = std::numeric_limits<float>::max();
    std::string bestPartId{};
    for (const auto& [partId, instance] : _previewParts)
    {
        const VoxelSpatialBounds bounds = evaluate_voxel_render_instance_bounds(instance);
        if (!bounds.valid)
        {
            continue;
        }

        const auto hit = voxel::picking::intersect_ray_box(ray, bounds.min, bounds.max, 500.0f);
        if (hit.has_value() && hit->distance < bestDistance)
        {
            bestDistance = hit->distance;
            bestPartId = partId;
        }
    }

    if (!bestPartId.empty())
    {
        _selectedPartId = bestPartId;
    }
}

void AnimationEditorScene::refresh_asset_lists()
{
    _assemblyAssetIds = _assemblyRepository.list_asset_ids();
    _clipAssetIds = _clipRepository.list_asset_ids();
    _controllerAssetIds = _controllerRepository.list_asset_ids();

    if (_selectedAssemblyId.empty() && !_assemblyAssetIds.empty())
    {
        _selectedAssemblyId = _assemblyAssetIds.front();
    }
}

void AnimationEditorScene::new_clip()
{
    _clipHistory.clear();
    _clip = VoxelAnimationClipAsset{};
    _clip.assetId = "untitled_clip";
    _clip.displayName = "Untitled Clip";
    _clip.assemblyAssetId = !_selectedAssemblyId.empty() ? _selectedAssemblyId : std::string{};
    _clip.durationSeconds = 1.0f;
    _clip.frameRateHint = 24.0f;
    _previewTimeSeconds = 0.0f;
    _previewDirty = true;
}

void AnimationEditorScene::save_clip()
{
    if (_clip.assemblyAssetId.empty())
    {
        _clip.assemblyAssetId = _selectedAssemblyId;
    }

    try
    {
        _clipRepository.save(_clip);
        _clipAssetManager.clear();
        refresh_asset_lists();
        _statusMessage = std::format("Saved clip '{}'.", _clip.assetId);
    }
    catch (const std::exception& ex)
    {
        _statusMessage = ex.what();
    }
}

void AnimationEditorScene::load_clip(const std::string& assetId)
{
    if (const auto clip = _clipRepository.load(assetId); clip.has_value())
    {
        _clip = clip.value();
        _selectedAssemblyId = _clip.assemblyAssetId;
        _clipHistory.clear();
        _previewTimeSeconds = 0.0f;
        _previewDirty = true;
        _statusMessage = std::format("Loaded clip '{}'.", _clip.assetId);
    }
}

void AnimationEditorScene::new_controller()
{
    _controllerHistory.clear();
    _controller = VoxelAnimationControllerAsset{};
    _controller.assetId = "untitled_controller";
    _controller.displayName = "Untitled Controller";
    _controller.assemblyAssetId = !_selectedAssemblyId.empty() ? _selectedAssemblyId : std::string{};
    _controllerPreviewDirty = true;
}

void AnimationEditorScene::save_controller()
{
    if (_controller.assemblyAssetId.empty())
    {
        _controller.assemblyAssetId = _selectedAssemblyId;
    }

    try
    {
        _controllerRepository.save(_controller);
        _controllerAssetManager.clear();
        _controllerPreviewDirty = false;
        refresh_asset_lists();
        _statusMessage = std::format("Saved controller '{}'.", _controller.assetId);
    }
    catch (const std::exception& ex)
    {
        _statusMessage = ex.what();
    }
}

void AnimationEditorScene::load_controller(const std::string& assetId)
{
    if (const auto controller = _controllerRepository.load(assetId); controller.has_value())
    {
        _controller = controller.value();
        _selectedAssemblyId = _controller.assemblyAssetId;
        _controllerHistory.clear();
        _controllerPreviewDirty = true;
        _statusMessage = std::format("Loaded controller '{}'.", _controller.assetId);
    }
}

void AnimationEditorScene::save_controller_preview_if_dirty()
{
    if (_mode != EditorMode::Controller || !_controllerPreviewDirty || _controller.assetId.empty() || _controller.assemblyAssetId.empty())
    {
        return;
    }

    try
    {
        _controllerRepository.save(_controller);
        _controllerAssetManager.clear();
        _controllerPreviewDirty = false;
    }
    catch (const std::exception&)
    {
    }
}

void AnimationEditorScene::apply_clip_edit(
    const std::string_view description,
    const std::function<void(VoxelAnimationClipAsset&)>& edit)
{
    if (editing::apply_snapshot_edit(_clipHistory, _clip, std::string(description), edit))
    {
        _previewDirty = true;
    }
}

void AnimationEditorScene::apply_controller_edit(
    const std::string_view description,
    const std::function<void(VoxelAnimationControllerAsset&)>& edit)
{
    if (editing::apply_snapshot_edit(_controllerHistory, _controller, std::string(description), edit))
    {
        _controllerPreviewDirty = true;
        _previewDirty = true;
    }
}

void AnimationEditorScene::sync_preview()
{
    if (!_previewDirty)
    {
        return;
    }

    _previewDirty = false;
    _previewRegistry.clear(_renderState);
    _previewParts.clear();
    _previewOrbitTarget = glm::vec3(0.0f);
    save_controller_preview_if_dirty();

    if (_selectedAssemblyId.empty())
    {
        return;
    }

    _previewAssembly.assetId = _selectedAssemblyId;
    const std::shared_ptr<const VoxelAssemblyAsset> assembly = _assemblyAssetManager.load_or_get(_selectedAssemblyId);
    if (assembly == nullptr)
    {
        return;
    }

    const VoxelAssemblyPose* pose = nullptr;
    VoxelAssemblyPose clipPreviewPose{};
    if (_mode == EditorMode::Clip && _clip.assemblyAssetId == _selectedAssemblyId)
    {
        clipPreviewPose = sample_voxel_animation_clip_pose(_clip, _previewTimeSeconds);
        pose = &clipPreviewPose;
    }
    else if (_mode == EditorMode::Controller && !_controller.assetId.empty())
    {
        _previewAnimation.controllerAssetId = _controller.assetId;
        set_voxel_animation_float_parameter(_previewAnimation, "move_x", _previewMoveX);
        set_voxel_animation_float_parameter(_previewAnimation, "move_y", _previewMoveY);
        set_voxel_animation_float_parameter(_previewAnimation, "speed", _previewSpeed);
        set_voxel_animation_bool_parameter(_previewAnimation, "grounded", _previewGrounded);
        set_voxel_animation_float_parameter(_previewAnimation, "vertical_speed", _previewVerticalSpeed);
        set_voxel_animation_bool_parameter(_previewAnimation, "fly_mode", _previewFlyMode);
        tick_voxel_animation_component(
            _previewAnimation,
            _previewAssembly,
            *assembly,
            _controllerAssetManager,
            _clipAssetManager,
            0.0f);
        pose = &_previewAnimation.currentPose;
    }

    const VoxelAssemblyRenderBundle bundle =
        build_voxel_assembly_render_bundle(_previewAssembly, _assemblyAssetManager, _assetManager, pose);

    glm::vec3 centerAccumulator{0.0f};
    size_t centerCount = 0;
    for (const auto& part : bundle.parts)
    {
        _previewParts.insert_or_assign(part.partId, part.renderInstance);
        const VoxelRenderRegistry::InstanceId instanceId = _previewRegistry.add_instance(part.renderInstance);
        (void)instanceId;

        const VoxelSpatialBounds bounds = evaluate_voxel_render_instance_bounds(part.renderInstance);
        if (bounds.valid)
        {
            centerAccumulator += bounds.center();
            ++centerCount;
        }
    }

    if (centerCount > 0)
    {
        _previewOrbitTarget = centerAccumulator / static_cast<float>(centerCount);
    }
}

void AnimationEditorScene::draw_editor_window()
{
    ImGui::Begin("Animation Editor");

    int mode = _mode == EditorMode::Clip ? 0 : 1;
    if (ImGui::RadioButton("Clip Mode", mode == 0))
    {
        _mode = EditorMode::Clip;
        _previewDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Controller Mode", mode == 1))
    {
        _mode = EditorMode::Controller;
        _previewDirty = true;
    }

    if (ImGui::BeginCombo("Assembly", _selectedAssemblyId.empty() ? "<none>" : _selectedAssemblyId.c_str()))
    {
        for (const std::string& assetId : _assemblyAssetIds)
        {
            const bool selected = assetId == _selectedAssemblyId;
            if (ImGui::Selectable(assetId.c_str(), selected))
            {
                _selectedAssemblyId = assetId;
                _clip.assemblyAssetId = assetId;
                _controller.assemblyAssetId = assetId;
                _previewDirty = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Playback");
    ImGui::Checkbox("Play", &_playing);
    ImGui::SameLine();
    if (ImGui::Button("Reset Time"))
    {
        _previewTimeSeconds = 0.0f;
        _previewDirty = true;
    }
    ImGui::SliderFloat("Time", &_previewTimeSeconds, 0.0f, std::max(_clip.durationSeconds, 0.1f));
    ImGui::Text("Selected Part: %s", _selectedPartId.empty() ? "<none>" : _selectedPartId.c_str());

    if (_mode == EditorMode::Controller)
    {
        ImGui::SliderFloat("Preview Move X", &_previewMoveX, -8.0f, 8.0f);
        ImGui::SliderFloat("Preview Move Y", &_previewMoveY, -8.0f, 8.0f);
        ImGui::SliderFloat("Preview Speed", &_previewSpeed, 0.0f, 12.0f);
        ImGui::Checkbox("Preview Grounded", &_previewGrounded);
        ImGui::SliderFloat("Preview Vertical Speed", &_previewVerticalSpeed, -20.0f, 20.0f);
        ImGui::Checkbox("Preview Fly Mode", &_previewFlyMode);
        _previewDirty = true;
    }

    ImGui::SeparatorText("Assets");
    if (_mode == EditorMode::Clip)
    {
        if (ImGui::BeginCombo("Saved Clips", _clip.assetId.c_str()))
        {
            for (const std::string& assetId : _clipAssetIds)
            {
                if (ImGui::Selectable(assetId.c_str(), assetId == _clip.assetId))
                {
                    load_clip(assetId);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("New Clip")) new_clip();
        ImGui::SameLine();
        if (ImGui::Button("Save Clip")) save_clip();
        ImGui::SameLine();
        if (ImGui::Button("Undo Clip") && _clipHistory.undo(_clip)) _previewDirty = true;
        ImGui::SameLine();
        if (ImGui::Button("Redo Clip") && _clipHistory.redo(_clip)) _previewDirty = true;
    }
    else
    {
        if (ImGui::BeginCombo("Saved Controllers", _controller.assetId.c_str()))
        {
            for (const std::string& assetId : _controllerAssetIds)
            {
                if (ImGui::Selectable(assetId.c_str(), assetId == _controller.assetId))
                {
                    load_controller(assetId);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("New Controller")) new_controller();
        ImGui::SameLine();
        if (ImGui::Button("Save Controller")) save_controller();
        ImGui::SameLine();
        if (ImGui::Button("Undo Controller") && _controllerHistory.undo(_controller)) { _previewDirty = true; _controllerPreviewDirty = true; }
        ImGui::SameLine();
        if (ImGui::Button("Redo Controller") && _controllerHistory.redo(_controller)) { _previewDirty = true; _controllerPreviewDirty = true; }
    }

    if (_mode == EditorMode::Clip)
    {
        ImGui::SeparatorText("Clip Inspector");
        ImGui::Text("Clip: %s", _clip.assetId.c_str());
        ImGui::InputFloat("Duration", &_clip.durationSeconds, 0.0f, 0.0f, "%.3f");
        ImGui::InputFloat("Frame Rate", &_clip.frameRateHint, 0.0f, 0.0f, "%.1f");

        const VoxelAssemblyPose sampledPose = sample_voxel_animation_clip_pose(_clip, _previewTimeSeconds);
        const VoxelAssemblyPosePart* const part = !_selectedPartId.empty() ? sampledPose.find_part(_selectedPartId) : nullptr;
        if (part != nullptr)
        {
            glm::vec3 position = part->localPosition.value_or(glm::vec3(0.0f));
            if (ImGui::InputFloat3("Local Position", &position.x, "%.3f"))
            {
                const std::string partId = _selectedPartId;
                const glm::vec3 nextPosition = position;
                const float time = _previewTimeSeconds;
                apply_clip_edit("Key local position", [partId, nextPosition, time](VoxelAnimationClipAsset& clip)
                {
                    auto* track = clip.find_part_track(partId);
                    if (track == nullptr)
                    {
                        clip.partTracks.push_back(VoxelAnimationPartTrack{ .partId = partId });
                        track = &clip.partTracks.back();
                    }
                    auto keyIt = std::ranges::find_if(track->transformKeys, [&](const VoxelAnimationTransformKeyframe& key) { return std::abs(key.timeSeconds - time) <= 0.0001f; });
                    if (keyIt == track->transformKeys.end())
                    {
                        track->transformKeys.push_back(VoxelAnimationTransformKeyframe{ .timeSeconds = time, .localPosition = nextPosition });
                    }
                    else
                    {
                        keyIt->localPosition = nextPosition;
                    }
                });
            }

            bool visible = part->visible.value_or(true);
            if (ImGui::Checkbox("Visible", &visible))
            {
                const std::string partId = _selectedPartId;
                const bool nextVisible = visible;
                const float time = _previewTimeSeconds;
                apply_clip_edit("Key visibility", [partId, nextVisible, time](VoxelAnimationClipAsset& clip)
                {
                    auto* track = clip.find_part_track(partId);
                    if (track == nullptr)
                    {
                        clip.partTracks.push_back(VoxelAnimationPartTrack{ .partId = partId });
                        track = &clip.partTracks.back();
                    }
                    auto keyIt = std::ranges::find_if(track->visibilityKeys, [&](const VoxelAnimationVisibilityKeyframe& key) { return std::abs(key.timeSeconds - time) <= 0.0001f; });
                    if (keyIt == track->visibilityKeys.end())
                    {
                        track->visibilityKeys.push_back(VoxelAnimationVisibilityKeyframe{ .timeSeconds = time, .visible = nextVisible });
                    }
                    else
                    {
                        keyIt->visible = nextVisible;
                    }
                });
            }
        }
    }
    else
    {
        ImGui::SeparatorText("Controller Inspector");
        ImGui::Text("Controller: %s", _controller.assetId.c_str());
        if (_controller.layers.empty())
        {
            if (ImGui::Button("Add Base Layer"))
            {
                apply_controller_edit("Add base layer", [](VoxelAnimationControllerAsset& controller)
                {
                    controller.layers.push_back(VoxelAnimationLayerDefinition{
                        .layerId = "base",
                        .displayName = "Base",
                        .blendMode = VoxelAnimationLayerBlendMode::Override
                    });
                });
            }
        }
        else
        {
            VoxelAnimationLayerDefinition& layer = _controller.layers.front();
            int blendMode = layer.blendMode == VoxelAnimationLayerBlendMode::Override ? 0 : 1;
            if (ImGui::Combo("Layer Blend", &blendMode, "Override\0Additive\0"))
            {
                apply_controller_edit("Edit layer blend", [blendMode](VoxelAnimationControllerAsset& controller)
                {
                    if (!controller.layers.empty())
                    {
                        controller.layers.front().blendMode = blendMode == 0
                            ? VoxelAnimationLayerBlendMode::Override
                            : VoxelAnimationLayerBlendMode::Additive;
                    }
                });
            }

            if (layer.states.empty())
            {
                if (ImGui::Button("Add State"))
                {
                    apply_controller_edit("Add state", [](VoxelAnimationControllerAsset& controller)
                    {
                        if (controller.layers.empty())
                        {
                            return;
                        }
                        controller.layers.front().states.push_back(VoxelAnimationStateDefinition{
                            .stateId = "state_0",
                            .displayName = "State 0"
                        });
                        controller.layers.front().entryStateId = controller.layers.front().states.front().stateId;
                    });
                }
            }
            else
            {
                VoxelAnimationStateDefinition& state = layer.states.front();
                ImGui::Text("State: %s", state.stateId.c_str());
                float playbackSpeed = state.playbackSpeed;
                if (ImGui::InputFloat("Playback Speed", &playbackSpeed, 0.0f, 0.0f, "%.3f"))
                {
                    apply_controller_edit("Edit playback speed", [playbackSpeed](VoxelAnimationControllerAsset& controller)
                    {
                        if (!controller.layers.empty() && !controller.layers.front().states.empty())
                        {
                            controller.layers.front().states.front().playbackSpeed = playbackSpeed;
                        }
                    });
                }

                int rootMotionMode = static_cast<int>(state.rootMotionMode);
                if (ImGui::Combo("Root Motion", &rootMotionMode, "Ignore\0Extract Planar\0Extract Full\0"))
                {
                    apply_controller_edit("Edit root motion mode", [rootMotionMode](VoxelAnimationControllerAsset& controller)
                    {
                        if (!controller.layers.empty() && !controller.layers.front().states.empty())
                        {
                            controller.layers.front().states.front().rootMotionMode = static_cast<RootMotionMode>(rootMotionMode);
                        }
                    });
                }
            }
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", _statusMessage.c_str());
    ImGui::End();
}
