#include "animation_editor_scene.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <ranges>

#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <SDL.h>

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "ImGuizmo.h"
#include "ImSequencer.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "orbit_orientation_gizmo.h"
#include "render/material_manager.h"
#include "render/mesh_manager.h"
#include "render/mesh_release_queue.h"
#include "string_utils.h"
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
    constexpr float KeyframeTimeEpsilon = 0.0001f;

    glm::vec3 orbit_front(const float yawDegrees, const float pitchDegrees)
    {
        const float yaw = glm::radians(yawDegrees);
        const float pitch = glm::radians(pitchDegrees);
        return glm::normalize(glm::vec3(
            std::cos(pitch) * std::cos(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::sin(yaw)));
    }

    glm::vec3 euler_degrees_from_quat(const glm::quat& rotation)
    {
        return glm::degrees(glm::eulerAngles(rotation));
    }

    [[nodiscard]] glm::quat quat_from_euler_degrees(const glm::vec3& rotationDegrees)
    {
        return glm::quat(glm::radians(rotationDegrees));
    }

    [[nodiscard]] glm::mat4 pivot_transform_matrix(
        const glm::vec3& position,
        const glm::quat& rotation,
        const float scale)
    {
        glm::mat4 result = glm::translate(glm::mat4(1.0f), position);
        result *= glm::mat4_cast(rotation);
        result = glm::scale(result, glm::vec3(scale));
        return result;
    }

    [[nodiscard]] glm::mat4 pivot_transform_matrix(const VoxelRenderInstance& instance)
    {
        const glm::vec3 pivotWorldPosition = (instance.asset != nullptr)
            ? instance.world_point_from_asset_local(instance.asset->model.pivot)
            : instance.position;
        return pivot_transform_matrix(pivotWorldPosition, instance.rotation, instance.scale);
    }

    [[nodiscard]] glm::mat4 parent_basis_matrix(
        const std::unordered_map<std::string, VoxelRenderInstance>& previewParts,
        const VoxelAssemblyBindingState& bindingState)
    {
        if (bindingState.parentPartId.empty())
        {
            return glm::mat4(1.0f);
        }

        const auto parentIt = previewParts.find(bindingState.parentPartId);
        if (parentIt == previewParts.end())
        {
            return glm::mat4(1.0f);
        }

        const VoxelRenderInstance& parentInstance = parentIt->second;
        if (!bindingState.parentAttachmentName.empty())
        {
            if (const std::optional<glm::mat4> attachmentTransform =
                parentInstance.attachment_world_transform(bindingState.parentAttachmentName);
                attachmentTransform.has_value())
            {
                return attachmentTransform.value();
            }
        }

        return pivot_transform_matrix(parentInstance);
    }

    [[nodiscard]] bool keyframe_time_matches(const float lhs, const float rhs)
    {
        return std::abs(lhs - rhs) <= KeyframeTimeEpsilon;
    }

    [[nodiscard]] int frame_from_time(const float timeSeconds, const float frameRate)
    {
        return static_cast<int>(std::round(timeSeconds * std::max(frameRate, 1.0f)));
    }

    [[nodiscard]] float time_from_frame(const int frame, const float frameRate)
    {
        return static_cast<float>(frame) / std::max(frameRate, 1.0f);
    }

    [[nodiscard]] float advance_preview_time(
        const float currentTimeSeconds,
        const float deltaTimeSeconds,
        const float durationSeconds,
        const VoxelAnimationLoopMode loopMode)
    {
        const float duration = std::max(durationSeconds, 0.001f);
        const float nextTime = currentTimeSeconds + deltaTimeSeconds;
        if (loopMode == VoxelAnimationLoopMode::Loop)
        {
            const float wrapped = std::fmod(nextTime, duration);
            return wrapped < 0.0f ? wrapped + duration : wrapped;
        }

        return std::clamp(nextTime, 0.0f, duration);
    }

    template <typename TKeyframe>
    void sort_keyframes(std::vector<TKeyframe>& keys)
    {
        std::ranges::sort(keys, [](const TKeyframe& lhs, const TKeyframe& rhs)
        {
            return lhs.timeSeconds < rhs.timeSeconds;
        });
    }

    VoxelAnimationPartTrack& ensure_part_track(VoxelAnimationClipAsset& clip, const std::string_view partId)
    {
        if (VoxelAnimationPartTrack* existing = clip.find_part_track(partId); existing != nullptr)
        {
            return *existing;
        }

        clip.partTracks.push_back(VoxelAnimationPartTrack{ .partId = std::string(partId) });
        return clip.partTracks.back();
    }

    VoxelAnimationBindingTrack& ensure_binding_track(VoxelAnimationClipAsset& clip, const std::string_view partId)
    {
        if (VoxelAnimationBindingTrack* existing = clip.find_binding_track(partId); existing != nullptr)
        {
            return *existing;
        }

        clip.bindingTracks.push_back(VoxelAnimationBindingTrack{ .partId = std::string(partId) });
        return clip.bindingTracks.back();
    }

    VoxelAnimationTransformKeyframe* find_transform_key(
        VoxelAnimationPartTrack& track,
        const float timeSeconds)
    {
        const auto it = std::ranges::find_if(track.transformKeys, [timeSeconds](const VoxelAnimationTransformKeyframe& key)
        {
            return keyframe_time_matches(key.timeSeconds, timeSeconds);
        });
        return it != track.transformKeys.end() ? &(*it) : nullptr;
    }

    VoxelAnimationVisibilityKeyframe* find_visibility_key(
        VoxelAnimationPartTrack& track,
        const float timeSeconds)
    {
        const auto it = std::ranges::find_if(track.visibilityKeys, [timeSeconds](const VoxelAnimationVisibilityKeyframe& key)
        {
            return keyframe_time_matches(key.timeSeconds, timeSeconds);
        });
        return it != track.visibilityKeys.end() ? &(*it) : nullptr;
    }

    VoxelAnimationBindingStateKeyframe* find_binding_key(
        VoxelAnimationBindingTrack& track,
        const float timeSeconds)
    {
        const auto it = std::ranges::find_if(track.keys, [timeSeconds](const VoxelAnimationBindingStateKeyframe& key)
        {
            return keyframe_time_matches(key.timeSeconds, timeSeconds);
        });
        return it != track.keys.end() ? &(*it) : nullptr;
    }

    VoxelAnimationTransformKeyframe& ensure_transform_key(
        VoxelAnimationPartTrack& track,
        const float timeSeconds,
        const VoxelAnimationTransformKeyframe& seed)
    {
        if (VoxelAnimationTransformKeyframe* existing = find_transform_key(track, timeSeconds); existing != nullptr)
        {
            return *existing;
        }

        track.transformKeys.push_back(seed);
        sort_keyframes(track.transformKeys);
        return *find_transform_key(track, timeSeconds);
    }

    VoxelAnimationVisibilityKeyframe& ensure_visibility_key(
        VoxelAnimationPartTrack& track,
        const float timeSeconds,
        const bool visible)
    {
        if (VoxelAnimationVisibilityKeyframe* existing = find_visibility_key(track, timeSeconds); existing != nullptr)
        {
            return *existing;
        }

        track.visibilityKeys.push_back(VoxelAnimationVisibilityKeyframe{
            .timeSeconds = timeSeconds,
            .visible = visible
        });
        sort_keyframes(track.visibilityKeys);
        return *find_visibility_key(track, timeSeconds);
    }

    VoxelAnimationBindingStateKeyframe& ensure_binding_key(
        VoxelAnimationBindingTrack& track,
        const float timeSeconds,
        const std::string_view stateId)
    {
        if (VoxelAnimationBindingStateKeyframe* existing = find_binding_key(track, timeSeconds); existing != nullptr)
        {
            return *existing;
        }

        track.keys.push_back(VoxelAnimationBindingStateKeyframe{
            .timeSeconds = timeSeconds,
            .stateId = std::string(stateId)
        });
        sort_keyframes(track.keys);
        return *find_binding_key(track, timeSeconds);
    }

    void erase_keys_at_time(
        VoxelAnimationClipAsset& clip,
        const std::string_view partId,
        const float timeSeconds)
    {
        if (VoxelAnimationPartTrack* partTrack = clip.find_part_track(partId); partTrack != nullptr)
        {
            std::erase_if(partTrack->transformKeys, [timeSeconds](const VoxelAnimationTransformKeyframe& key)
            {
                return keyframe_time_matches(key.timeSeconds, timeSeconds);
            });
            std::erase_if(partTrack->visibilityKeys, [timeSeconds](const VoxelAnimationVisibilityKeyframe& key)
            {
                return keyframe_time_matches(key.timeSeconds, timeSeconds);
            });
        }

        if (VoxelAnimationBindingTrack* bindingTrack = clip.find_binding_track(partId); bindingTrack != nullptr)
        {
            std::erase_if(bindingTrack->keys, [timeSeconds](const VoxelAnimationBindingStateKeyframe& key)
            {
                return keyframe_time_matches(key.timeSeconds, timeSeconds);
            });
        }
    }

    void prune_empty_tracks(VoxelAnimationClipAsset& clip)
    {
        std::erase_if(clip.partTracks, [](const VoxelAnimationPartTrack& track)
        {
            return track.transformKeys.empty() && track.visibilityKeys.empty();
        });
        std::erase_if(clip.bindingTracks, [](const VoxelAnimationBindingTrack& track)
        {
            return track.keys.empty();
        });
    }

    enum class ClipSequencerLaneType
    {
        Transform,
        Visibility,
        Binding,
        Event
    };

    struct ClipSequencerEntry
    {
        ClipSequencerLaneType laneType{ClipSequencerLaneType::Transform};
        std::string label{};
        std::string partId{};
        std::string trackId{};
        std::vector<int> keyFrames{};
        int frameStart{0};
        int frameEnd{1};
        unsigned int color{0xFF8CC8FFu};
    };

    class ClipSequencerModel final : public ImSequencer::SequenceInterface
    {
    public:
        ClipSequencerModel(
            std::vector<ClipSequencerEntry> entries,
            const int frameMin,
            const int frameMax) :
            _entries(std::move(entries)),
            _frameMin(frameMin),
            _frameMax(std::max(frameMax, frameMin + 1))
        {
        }

        [[nodiscard]] int GetFrameMin() const override
        {
            return _frameMin;
        }

        [[nodiscard]] int GetFrameMax() const override
        {
            return _frameMax;
        }

        [[nodiscard]] int GetItemCount() const override
        {
            return static_cast<int>(_entries.size());
        }

        [[nodiscard]] int GetItemTypeCount() const override
        {
            return 4;
        }

        [[nodiscard]] const char* GetItemTypeName(const int typeIndex) const override
        {
            static constexpr const char* TypeNames[] = {
                "Transform",
                "Visibility",
                "Binding",
                "Event"
            };
            return typeIndex >= 0 && typeIndex < static_cast<int>(std::size(TypeNames))
                ? TypeNames[typeIndex]
                : "Track";
        }

        [[nodiscard]] const char* GetItemLabel(const int index) const override
        {
            return _entries[static_cast<size_t>(index)].label.c_str();
        }

        void Get(int index, int** start, int** end, int* type, unsigned int* color) override
        {
            ClipSequencerEntry& entry = _entries[static_cast<size_t>(index)];
            if (start != nullptr)
            {
                *start = &entry.frameStart;
            }
            if (end != nullptr)
            {
                *end = &entry.frameEnd;
            }
            if (type != nullptr)
            {
                *type = static_cast<int>(entry.laneType);
            }
            if (color != nullptr)
            {
                *color = entry.color;
            }
        }

        void CustomDrawCompact(const int index, ImDrawList* drawList, const ImRect& rc, const ImRect& clippingRect) override
        {
            const ClipSequencerEntry& entry = _entries[static_cast<size_t>(index)];
            if (entry.keyFrames.empty() || _frameMax <= _frameMin)
            {
                return;
            }

            drawList->PushClipRect(clippingRect.Min, clippingRect.Max, true);
            for (const int keyFrame : entry.keyFrames)
            {
                const float t = std::clamp(
                    static_cast<float>(keyFrame - _frameMin) / static_cast<float>(_frameMax - _frameMin),
                    0.0f,
                    1.0f);
                const float x = rc.Min.x + ((rc.Max.x - rc.Min.x) * t);
                drawList->AddLine(
                    ImVec2(x, rc.Min.y + 4.0f),
                    ImVec2(x, rc.Max.y - 3.0f),
                    entry.color,
                    2.5f);
            }
            drawList->PopClipRect();
        }

        [[nodiscard]] const std::vector<ClipSequencerEntry>& entries() const
        {
            return _entries;
        }

    private:
        std::vector<ClipSequencerEntry> _entries{};
        int _frameMin{0};
        int _frameMax{1};
    };

    std::vector<ClipSequencerEntry> build_clip_sequencer_entries(const VoxelAnimationClipAsset& clip)
    {
        std::vector<ClipSequencerEntry> entries{};
        const float frameRate = std::max(clip.frameRateHint, 1.0f);

        auto build_range = [](const std::vector<int>& keyFrames) -> std::pair<int, int>
        {
            if (keyFrames.empty())
            {
                return { 0, 1 };
            }

            const auto [minIt, maxIt] = std::minmax_element(keyFrames.begin(), keyFrames.end());
            const int start = *minIt;
            const int end = std::max(*maxIt, start + 1);
            return { start, end };
        };

        for (const VoxelAnimationPartTrack& track : clip.partTracks)
        {
            if (!track.transformKeys.empty())
            {
                ClipSequencerEntry entry{};
                entry.laneType = ClipSequencerLaneType::Transform;
                entry.label = std::format("{} / Transform", track.partId);
                entry.partId = track.partId;
                entry.color = 0xFF7CC8FFu;
                entry.keyFrames.reserve(track.transformKeys.size());
                for (const VoxelAnimationTransformKeyframe& key : track.transformKeys)
                {
                    entry.keyFrames.push_back(frame_from_time(key.timeSeconds, frameRate));
                }
                const auto [frameStart, frameEnd] = build_range(entry.keyFrames);
                entry.frameStart = frameStart;
                entry.frameEnd = frameEnd;
                entries.push_back(std::move(entry));
            }

            if (!track.visibilityKeys.empty())
            {
                ClipSequencerEntry entry{};
                entry.laneType = ClipSequencerLaneType::Visibility;
                entry.label = std::format("{} / Visibility", track.partId);
                entry.partId = track.partId;
                entry.color = 0xFF7CFF9Eu;
                entry.keyFrames.reserve(track.visibilityKeys.size());
                for (const VoxelAnimationVisibilityKeyframe& key : track.visibilityKeys)
                {
                    entry.keyFrames.push_back(frame_from_time(key.timeSeconds, frameRate));
                }
                const auto [frameStart, frameEnd] = build_range(entry.keyFrames);
                entry.frameStart = frameStart;
                entry.frameEnd = frameEnd;
                entries.push_back(std::move(entry));
            }
        }

        for (const VoxelAnimationBindingTrack& track : clip.bindingTracks)
        {
            if (track.keys.empty())
            {
                continue;
            }

            ClipSequencerEntry entry{};
            entry.laneType = ClipSequencerLaneType::Binding;
            entry.label = std::format("{} / Binding", track.partId);
            entry.partId = track.partId;
            entry.color = 0xFFFFBE72u;
            entry.keyFrames.reserve(track.keys.size());
            for (const VoxelAnimationBindingStateKeyframe& key : track.keys)
            {
                entry.keyFrames.push_back(frame_from_time(key.timeSeconds, frameRate));
            }
            const auto [frameStart, frameEnd] = build_range(entry.keyFrames);
            entry.frameStart = frameStart;
            entry.frameEnd = frameEnd;
            entries.push_back(std::move(entry));
        }

        for (const VoxelAnimationEventTrack& track : clip.eventTracks)
        {
            if (track.events.empty())
            {
                continue;
            }

            ClipSequencerEntry entry{};
            entry.laneType = ClipSequencerLaneType::Event;
            entry.label = std::format("{} / Events", track.trackId.empty() ? "event_track" : track.trackId);
            entry.trackId = track.trackId;
            entry.color = 0xFFE08BFFu;
            entry.keyFrames.reserve(track.events.size());
            for (const VoxelAnimationEventKeyframe& key : track.events)
            {
                entry.keyFrames.push_back(frame_from_time(key.timeSeconds, frameRate));
            }
            const auto [frameStart, frameEnd] = build_range(entry.keyFrames);
            entry.frameStart = frameStart;
            entry.frameEnd = frameEnd;
            entries.push_back(std::move(entry));
        }

        std::ranges::sort(entries, [](const ClipSequencerEntry& lhs, const ClipSequencerEntry& rhs)
        {
            if (lhs.partId != rhs.partId)
            {
                if (lhs.partId.empty())
                {
                    return false;
                }
                if (rhs.partId.empty())
                {
                    return true;
                }
                return lhs.partId < rhs.partId;
            }

            return lhs.label < rhs.label;
        });

        return entries;
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
    if (_selectedPartBoundsHandle.has_value())
    {
        _renderState.transparentObjects.remove(_selectedPartBoundsHandle.value());
        _selectedPartBoundsHandle.reset();
    }
    release_selection_meshes();
}

void AnimationEditorScene::update_buffers()
{
    sync_preview();
    _previewRegistry.sync(*_services.meshManager, *_services.materialManager, AnimationEditorMaterialScope, _renderState);
    sync_selection_overlay();
    update_uniform_buffers();
}

void AnimationEditorScene::update(const float deltaTime)
{
    if (_playing)
    {
        if (_mode == EditorMode::Clip)
        {
            _previewTimeSeconds = advance_preview_time(
                _previewTimeSeconds,
                deltaTime,
                _clip.durationSeconds,
                _clip.loopMode);
        }
        else
        {
            _previewTimeSeconds += deltaTime;
        }
        _previewDirty = true;
        _selectionOverlayDirty = true;
    }

    update_camera();
}

void AnimationEditorScene::handle_input(const SDL_Event& event)
{
    if (USE_IMGUI && ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION) &&
        (ImGuizmo::IsOver() || ImGuizmo::IsUsing()))
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
    ImGuizmo::BeginFrame();
    draw_editor_window();
    draw_transform_gizmo();
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

    _services.materialManager->build_graphics_pipeline(
        AnimationEditorMaterialScope,
        {
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _lightingResource)
        },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .blendMode = BlendMode::Alpha, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
        "tri_mesh.vert.spv",
        "tri_mesh.frag.spv",
        "chunkboundary");

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
        _selectionOverlayDirty = true;
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
    _selectionOverlayDirty = true;
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

    if (!_selectedPartId.empty() && !_previewParts.contains(_selectedPartId))
    {
        _selectedPartId.clear();
    }
    if (_selectedPartId.empty() && !bundle.parts.empty())
    {
        _selectedPartId = bundle.parts.front().partId;
    }
}

void AnimationEditorScene::sync_selection_overlay()
{
    if (!_selectionOverlayDirty)
    {
        return;
    }

    _selectionOverlayDirty = false;

    if (_selectedPartBoundsHandle.has_value())
    {
        _renderState.transparentObjects.remove(_selectedPartBoundsHandle.value());
        _selectedPartBoundsHandle.reset();
    }
    release_selection_meshes();

    if (!_showSelectedPartBounds || _selectedPartId.empty())
    {
        return;
    }

    const auto previewIt = _previewParts.find(_selectedPartId);
    if (previewIt == _previewParts.end())
    {
        return;
    }

    const VoxelRenderInstance& instance = previewIt->second;
    if (!instance.is_renderable() || instance.asset == nullptr)
    {
        return;
    }

    const VoxelSpatialBounds localBounds = evaluate_voxel_model_local_bounds(instance.asset->model);
    if (!localBounds.valid)
    {
        return;
    }

    _selectedPartBoundsMesh = Mesh::create_box_outline_mesh(localBounds.min, localBounds.max, glm::vec3(0.28f, 0.92f, 1.0f));
    _services.meshManager->UploadQueue.enqueue(_selectedPartBoundsMesh);
    _selectedPartBoundsHandle = _renderState.transparentObjects.insert(RenderObject{
        .mesh = _selectedPartBoundsMesh,
        .material = _services.materialManager->get_material(AnimationEditorMaterialScope, "chunkboundary"),
        .transform = instance.model_matrix(),
        .layer = RenderLayer::Transparent,
        .lightingMode = LightingMode::Unlit
    });
}

void AnimationEditorScene::release_selection_meshes()
{
    if (_selectedPartBoundsMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_selectedPartBoundsMesh));
    }
}

void AnimationEditorScene::draw_transform_gizmo()
{
    if (_mode != EditorMode::Clip || !_showTransformGizmo || _selectedPartId.empty())
    {
        return;
    }

    const auto previewIt = _previewParts.find(_selectedPartId);
    if (previewIt == _previewParts.end())
    {
        return;
    }

    const std::shared_ptr<const VoxelAssemblyAsset> selectedAssembly =
        !_selectedAssemblyId.empty() ? _assemblyAssetManager.load_or_get(_selectedAssemblyId) : nullptr;
    if (selectedAssembly == nullptr)
    {
        return;
    }

    const VoxelAssemblyPartDefinition* const selectedPart = selectedAssembly->find_part(_selectedPartId);
    if (selectedPart == nullptr)
    {
        return;
    }

    std::string effectiveBindingStateId = selectedPart->defaultStateId;
    if (_mode == EditorMode::Clip && _clip.assemblyAssetId == _selectedAssemblyId)
    {
        const VoxelAssemblyPose sampledPose = sample_voxel_animation_clip_pose(_clip, _previewTimeSeconds);
        if (const VoxelAssemblyPosePart* const posePart = sampledPose.find_part(_selectedPartId);
            posePart != nullptr && posePart->bindingStateId.has_value())
        {
            effectiveBindingStateId = posePart->bindingStateId.value();
        }
    }

    const VoxelAssemblyBindingState* const bindingState =
        !effectiveBindingStateId.empty() ? selectedAssembly->find_binding_state(_selectedPartId, effectiveBindingStateId) : nullptr;
    if (bindingState == nullptr)
    {
        return;
    }

    ImGuiViewport* const viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    const VoxelRenderInstance& instance = previewIt->second;
    glm::mat4 worldMatrix = pivot_transform_matrix(instance);
    const glm::mat4 parentBasis = parent_basis_matrix(_previewParts, *bindingState);

    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, viewport->WorkSize.y);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::Enable(true);

    float snap[3]{};
    float* snapPtr = nullptr;
    const ImGuizmo::OPERATION operation = _gizmoOperation == 1 ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;
    const ImGuizmo::MODE mode = _gizmoMode == 0 ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    if (operation == ImGuizmo::TRANSLATE)
    {
        const float translationSnap = std::max(instance.asset != nullptr ? instance.asset->model.voxelSize : (1.0f / 16.0f), 0.001f);
        snap[0] = translationSnap;
        snap[1] = translationSnap;
        snap[2] = translationSnap;
        snapPtr = snap;
    }

    if (ImGuizmo::Manipulate(
        glm::value_ptr(_camera->_view),
        glm::value_ptr(_camera->_projection),
        operation,
        mode,
        glm::value_ptr(worldMatrix),
        nullptr,
        snapPtr))
    {
        const glm::mat4 localMatrix = glm::inverse(parentBasis) * worldMatrix;
        float translation[3]{};
        float rotation[3]{};
        float scale[3]{};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localMatrix), translation, rotation, scale);

        const glm::vec3 nextPosition = glm::vec3(translation[0], translation[1], translation[2]);
        const glm::quat nextRotation = quat_from_euler_degrees(glm::vec3(rotation[0], rotation[1], rotation[2]));
        const glm::vec3 nextScale = glm::max(glm::vec3(scale[0], scale[1], scale[2]), glm::vec3(0.001f));
        const std::string partId = selectedPart->partId;
        const float time = _previewTimeSeconds;

        apply_clip_edit("Manipulate transform gizmo", [partId, time, nextPosition, nextRotation, nextScale](VoxelAnimationClipAsset& clip)
        {
            VoxelAnimationPartTrack& track = ensure_part_track(clip, partId);
            VoxelAnimationTransformKeyframe& key = ensure_transform_key(track, time, VoxelAnimationTransformKeyframe{
                .timeSeconds = time,
                .localPosition = nextPosition,
                .localRotation = nextRotation,
                .localScale = nextScale
            });
            key.localPosition = nextPosition;
            key.localRotation = nextRotation;
            key.localScale = nextScale;
        });
    }
}

void AnimationEditorScene::draw_editor_window()
{
    ImGui::Begin("Animation Editor");

    const std::shared_ptr<const VoxelAssemblyAsset> selectedAssembly =
        !_selectedAssemblyId.empty() ? _assemblyAssetManager.load_or_get(_selectedAssemblyId) : nullptr;

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
                _selectedPartId.clear();
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
    if (ImGui::SliderFloat("Time", &_previewTimeSeconds, 0.0f, std::max(_clip.durationSeconds, 0.1f)))
    {
        _previewDirty = true;
    }
    ImGui::Text("Selected Part: %s", _selectedPartId.empty() ? "<none>" : _selectedPartId.c_str());
    if (ImGui::Checkbox("Show Transform Gizmo", &_showTransformGizmo))
    {
        _previewDirty = true;
    }
    if (_showTransformGizmo)
    {
        ImGui::SameLine();
        ImGui::RadioButton("Translate", &_gizmoOperation, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Rotate", &_gizmoOperation, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Local", &_gizmoMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("World", &_gizmoMode, 1);
    }

    if (_mode == EditorMode::Controller)
    {
        bool previewChanged = false;
        previewChanged |= ImGui::SliderFloat("Preview Move X", &_previewMoveX, -8.0f, 8.0f);
        previewChanged |= ImGui::SliderFloat("Preview Move Y", &_previewMoveY, -8.0f, 8.0f);
        previewChanged |= ImGui::SliderFloat("Preview Speed", &_previewSpeed, 0.0f, 12.0f);
        previewChanged |= ImGui::Checkbox("Preview Grounded", &_previewGrounded);
        previewChanged |= ImGui::SliderFloat("Preview Vertical Speed", &_previewVerticalSpeed, -20.0f, 20.0f);
        previewChanged |= ImGui::Checkbox("Preview Fly Mode", &_previewFlyMode);
        if (previewChanged)
        {
            _previewDirty = true;
        }
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
        char clipAssetIdBuffer[128]{};
        copy_cstr_truncating(clipAssetIdBuffer, _clip.assetId);
        if (ImGui::InputText("Clip Asset Id", clipAssetIdBuffer, IM_ARRAYSIZE(clipAssetIdBuffer)))
        {
            const std::string nextAssetId = clipAssetIdBuffer;
            apply_clip_edit("Rename clip asset id", [nextAssetId](VoxelAnimationClipAsset& clip)
            {
                clip.assetId = nextAssetId;
            });
        }

        char clipDisplayNameBuffer[128]{};
        copy_cstr_truncating(clipDisplayNameBuffer, _clip.displayName);
        if (ImGui::InputText("Clip Display Name", clipDisplayNameBuffer, IM_ARRAYSIZE(clipDisplayNameBuffer)))
        {
            const std::string nextDisplayName = clipDisplayNameBuffer;
            apply_clip_edit("Rename clip display name", [nextDisplayName](VoxelAnimationClipAsset& clip)
            {
                clip.displayName = nextDisplayName;
            });
        }

        float durationSeconds = _clip.durationSeconds;
        if (ImGui::InputFloat("Duration", &durationSeconds, 0.0f, 0.0f, "%.3f"))
        {
            const float nextDuration = std::max(durationSeconds, 0.001f);
            apply_clip_edit("Edit clip duration", [nextDuration](VoxelAnimationClipAsset& clip)
            {
                clip.durationSeconds = nextDuration;
            });
            _previewTimeSeconds = std::clamp(_previewTimeSeconds, 0.0f, nextDuration);
        }

        float frameRateHint = _clip.frameRateHint;
        if (ImGui::InputFloat("Frame Rate", &frameRateHint, 0.0f, 0.0f, "%.1f"))
        {
            const float nextFrameRate = std::max(frameRateHint, 1.0f);
            apply_clip_edit("Edit clip frame rate", [nextFrameRate](VoxelAnimationClipAsset& clip)
            {
                clip.frameRateHint = nextFrameRate;
            });
        }

        int loopMode = _clip.loopMode == VoxelAnimationLoopMode::Loop ? 1 : 0;
        if (ImGui::Combo("Loop Mode", &loopMode, "Once\0Loop\0"))
        {
            const VoxelAnimationLoopMode nextLoopMode = loopMode == 0 ? VoxelAnimationLoopMode::Once : VoxelAnimationLoopMode::Loop;
            apply_clip_edit("Edit clip loop mode", [nextLoopMode](VoxelAnimationClipAsset& clip)
            {
                clip.loopMode = nextLoopMode;
            });
        }

        const char* motionSourceLabel = _clip.motionSourcePartId.empty() ? "<assembly root>" : _clip.motionSourcePartId.c_str();
        if (ImGui::BeginCombo("Motion Source Part", motionSourceLabel))
        {
            const bool rootSelected = _clip.motionSourcePartId.empty();
            if (ImGui::Selectable("<assembly root>", rootSelected))
            {
                apply_clip_edit("Clear motion source part", [](VoxelAnimationClipAsset& clip)
                {
                    clip.motionSourcePartId.clear();
                });
            }

            if (selectedAssembly != nullptr)
            {
                for (const VoxelAssemblyPartDefinition& assemblyPart : selectedAssembly->parts)
                {
                    const bool selected = _clip.motionSourcePartId == assemblyPart.partId;
                    if (ImGui::Selectable(assemblyPart.partId.c_str(), selected))
                    {
                        const std::string nextMotionSourcePartId = assemblyPart.partId;
                        apply_clip_edit("Edit motion source part", [nextMotionSourcePartId](VoxelAnimationClipAsset& clip)
                        {
                            clip.motionSourcePartId = nextMotionSourcePartId;
                        });
                    }
                }
            }
            ImGui::EndCombo();
        }

        const std::string fallbackRootPartId = selectedAssembly != nullptr ? selectedAssembly->rootPartId : std::string{};
        const glm::vec3 motionSample = sample_voxel_animation_clip_motion_source_position(_clip, fallbackRootPartId, _previewTimeSeconds);
        ImGui::Text("Motion Sample: %.3f %.3f %.3f", motionSample.x, motionSample.y, motionSample.z);
        ImGui::Text("Current Frame: %d", frame_from_time(_previewTimeSeconds, _clip.frameRateHint));

        if (ImGui::Checkbox("Show Selected Bounds", &_showSelectedPartBounds))
        {
            _selectionOverlayDirty = true;
        }

        if (selectedAssembly == nullptr)
        {
            ImGui::TextWrapped("Select an assembly to author a clip.");
        }
        else
        {
            ImGui::SeparatorText("Sequencer");
            const float frameRate = std::max(_clip.frameRateHint, 1.0f);
            const int maxFrame = std::max(frame_from_time(_clip.durationSeconds, frameRate), 1);
            std::vector<ClipSequencerEntry> sequencerEntries = build_clip_sequencer_entries(_clip);
            if (sequencerEntries.empty())
            {
                ImGui::TextWrapped("No track lanes yet. Key transforms, visibility, binding states, or events and they will appear here.");
            }
            else
            {
                if (!_selectedPartId.empty())
                {
                    const bool currentSelectionMatchesPart =
                        _sequencerSelectedEntry >= 0 &&
                        _sequencerSelectedEntry < static_cast<int>(sequencerEntries.size()) &&
                        sequencerEntries[static_cast<size_t>(_sequencerSelectedEntry)].partId == _selectedPartId;
                    const auto matchingEntry = std::ranges::find_if(sequencerEntries, [&](const ClipSequencerEntry& entry)
                    {
                        return entry.partId == _selectedPartId;
                    });
                    if (!currentSelectionMatchesPart && matchingEntry != sequencerEntries.end())
                    {
                        _sequencerSelectedEntry = static_cast<int>(std::distance(sequencerEntries.begin(), matchingEntry));
                    }
                }

                ClipSequencerModel sequencer(std::move(sequencerEntries), 0, maxFrame);
                int currentFrame = std::clamp(frame_from_time(_previewTimeSeconds, frameRate), 0, maxFrame);
                const int initialFrame = currentFrame;
                int selectedEntry = std::clamp(_sequencerSelectedEntry, -1, sequencer.GetItemCount() - 1);
                bool expanded = _sequencerExpanded;
                int firstFrame = std::clamp(_sequencerFirstFrame, 0, maxFrame);

                ImSequencer::Sequencer(
                    &sequencer,
                    &currentFrame,
                    &expanded,
                    &selectedEntry,
                    &firstFrame,
                    ImSequencer::SEQUENCER_CHANGE_FRAME);

                _sequencerExpanded = expanded;
                _sequencerSelectedEntry = selectedEntry;
                _sequencerFirstFrame = firstFrame;

                if (currentFrame != initialFrame)
                {
                    const float nextPreviewTime = std::clamp(time_from_frame(currentFrame, frameRate), 0.0f, _clip.durationSeconds);
                    if (!keyframe_time_matches(_previewTimeSeconds, nextPreviewTime))
                    {
                        _previewTimeSeconds = nextPreviewTime;
                        _previewDirty = true;
                    }
                }

                if (selectedEntry >= 0 && selectedEntry < sequencer.GetItemCount())
                {
                    const ClipSequencerEntry& entry = sequencer.entries()[static_cast<size_t>(selectedEntry)];
                    if (!entry.partId.empty() && _selectedPartId != entry.partId)
                    {
                        _selectedPartId = entry.partId;
                        _selectionOverlayDirty = true;
                    }

                    ImGui::TextWrapped("Selected Lane: %s", entry.label.c_str());
                }
            }

            ImGui::SeparatorText("Assembly Parts");
            if (ImGui::BeginChild("ClipPartList", ImVec2(0.0f, 140.0f), true))
            {
                for (const VoxelAssemblyPartDefinition& assemblyPart : selectedAssembly->parts)
                {
                    const bool selected = _selectedPartId == assemblyPart.partId;
                    if (ImGui::Selectable(assemblyPart.partId.c_str(), selected))
                    {
                        _selectedPartId = assemblyPart.partId;
                        _selectionOverlayDirty = true;
                    }
                }
            }
            ImGui::EndChild();

            const VoxelAssemblyPartDefinition* const selectedPart =
                !_selectedPartId.empty() ? selectedAssembly->find_part(_selectedPartId) : nullptr;
            if (selectedPart == nullptr)
            {
                ImGui::TextWrapped("Pick a part in the viewport or select one from the list to edit its keys.");
            }
            else
            {
                const VoxelAssemblyPose sampledPose = sample_voxel_animation_clip_pose(_clip, _previewTimeSeconds);
                const VoxelAssemblyPosePart* const sampledPart = sampledPose.find_part(selectedPart->partId);
                const std::string effectiveBindingStateId =
                    sampledPart != nullptr && sampledPart->bindingStateId.has_value()
                    ? sampledPart->bindingStateId.value()
                    : selectedPart->defaultStateId;
                const VoxelAssemblyBindingState* const effectiveBindingState =
                    !effectiveBindingStateId.empty() ? selectedAssembly->find_binding_state(selectedPart->partId, effectiveBindingStateId) : nullptr;

                const glm::vec3 effectivePosition =
                    sampledPart != nullptr && sampledPart->localPosition.has_value()
                    ? sampledPart->localPosition.value()
                    : (effectiveBindingState != nullptr ? effectiveBindingState->localPositionOffset : glm::vec3(0.0f));
                const glm::quat effectiveRotation =
                    sampledPart != nullptr && sampledPart->localRotation.has_value()
                    ? sampledPart->localRotation.value()
                    : (effectiveBindingState != nullptr ? effectiveBindingState->localRotationOffset : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                const glm::vec3 effectiveScale =
                    sampledPart != nullptr && sampledPart->localScale.has_value()
                    ? sampledPart->localScale.value()
                    : (effectiveBindingState != nullptr ? effectiveBindingState->localScale : glm::vec3(1.0f));
                const bool effectiveVisible =
                    sampledPart != nullptr && sampledPart->visible.has_value()
                    ? sampledPart->visible.value()
                    : (effectiveBindingState != nullptr ? effectiveBindingState->visible : selectedPart->visibleByDefault);

                const VoxelAnimationPartTrack* const partTrack = _clip.find_part_track(selectedPart->partId);
                const VoxelAnimationBindingTrack* const bindingTrack = _clip.find_binding_track(selectedPart->partId);

                ImGui::SeparatorText("Selected Part");
                ImGui::Text("Part: %s", selectedPart->partId.c_str());
                if (!selectedPart->displayName.empty())
                {
                    ImGui::Text("Display Name: %s", selectedPart->displayName.c_str());
                }
                ImGui::Text("Default State: %s", selectedPart->defaultStateId.empty() ? "<none>" : selectedPart->defaultStateId.c_str());
                ImGui::Text("Current Binding: %s", effectiveBindingStateId.empty() ? "<default>" : effectiveBindingStateId.c_str());

                if (ImGui::Button("Key Transform At Time"))
                {
                    const std::string partId = selectedPart->partId;
                    const float time = _previewTimeSeconds;
                    const glm::vec3 nextPosition = effectivePosition;
                    const glm::quat nextRotation = effectiveRotation;
                    const glm::vec3 nextScale = effectiveScale;
                    apply_clip_edit("Key transform", [partId, time, nextPosition, nextRotation, nextScale](VoxelAnimationClipAsset& clip)
                    {
                        VoxelAnimationPartTrack& track = ensure_part_track(clip, partId);
                        VoxelAnimationTransformKeyframe& key = ensure_transform_key(track, time, VoxelAnimationTransformKeyframe{
                            .timeSeconds = time,
                            .localPosition = nextPosition,
                            .localRotation = nextRotation,
                            .localScale = nextScale
                        });
                        key.localPosition = nextPosition;
                        key.localRotation = nextRotation;
                        key.localScale = nextScale;
                    });
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete Keys At Time"))
                {
                    const std::string partId = selectedPart->partId;
                    const float time = _previewTimeSeconds;
                    apply_clip_edit("Delete keys at time", [partId, time](VoxelAnimationClipAsset& clip)
                    {
                        erase_keys_at_time(clip, partId, time);
                        prune_empty_tracks(clip);
                    });
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Part Tracks"))
                {
                    const std::string partId = selectedPart->partId;
                    apply_clip_edit("Clear part tracks", [partId](VoxelAnimationClipAsset& clip)
                    {
                        std::erase_if(clip.partTracks, [&partId](const VoxelAnimationPartTrack& track)
                        {
                            return track.partId == partId;
                        });
                        std::erase_if(clip.bindingTracks, [&partId](const VoxelAnimationBindingTrack& track)
                        {
                            return track.partId == partId;
                        });
                    });
                }

                glm::vec3 localPosition = effectivePosition;
                if (ImGui::InputFloat3("Local Position", &localPosition.x, "%.3f"))
                {
                    const std::string partId = selectedPart->partId;
                    const float time = _previewTimeSeconds;
                    const glm::vec3 nextPosition = localPosition;
                    const glm::quat nextRotation = effectiveRotation;
                    const glm::vec3 nextScale = effectiveScale;
                    apply_clip_edit("Key local position", [partId, time, nextPosition, nextRotation, nextScale](VoxelAnimationClipAsset& clip)
                    {
                        VoxelAnimationPartTrack& track = ensure_part_track(clip, partId);
                        VoxelAnimationTransformKeyframe& key = ensure_transform_key(track, time, VoxelAnimationTransformKeyframe{
                            .timeSeconds = time,
                            .localPosition = nextPosition,
                            .localRotation = nextRotation,
                            .localScale = nextScale
                        });
                        key.localPosition = nextPosition;
                    });
                }

                glm::vec3 localRotationDegrees = euler_degrees_from_quat(effectiveRotation);
                if (ImGui::InputFloat3("Local Rotation (Deg)", &localRotationDegrees.x, "%.1f"))
                {
                    const std::string partId = selectedPart->partId;
                    const float time = _previewTimeSeconds;
                    const glm::vec3 nextDegrees = localRotationDegrees;
                    const glm::vec3 nextPosition = effectivePosition;
                    const glm::vec3 nextScale = effectiveScale;
                    apply_clip_edit("Key local rotation", [partId, time, nextDegrees, nextPosition, nextScale](VoxelAnimationClipAsset& clip)
                    {
                        VoxelAnimationPartTrack& track = ensure_part_track(clip, partId);
                        VoxelAnimationTransformKeyframe& key = ensure_transform_key(track, time, VoxelAnimationTransformKeyframe{
                            .timeSeconds = time,
                            .localPosition = nextPosition,
                            .localRotation = glm::quat(glm::radians(nextDegrees)),
                            .localScale = nextScale
                        });
                        key.localRotation = glm::quat(glm::radians(nextDegrees));
                    });
                }

                glm::vec3 localScale = effectiveScale;
                if (ImGui::InputFloat3("Local Scale", &localScale.x, "%.3f"))
                {
                    const std::string partId = selectedPart->partId;
                    const float time = _previewTimeSeconds;
                    const glm::vec3 nextScale = glm::max(localScale, glm::vec3(0.001f));
                    const glm::vec3 nextPosition = effectivePosition;
                    const glm::quat nextRotation = effectiveRotation;
                    apply_clip_edit("Key local scale", [partId, time, nextScale, nextPosition, nextRotation](VoxelAnimationClipAsset& clip)
                    {
                        VoxelAnimationPartTrack& track = ensure_part_track(clip, partId);
                        VoxelAnimationTransformKeyframe& key = ensure_transform_key(track, time, VoxelAnimationTransformKeyframe{
                            .timeSeconds = time,
                            .localPosition = nextPosition,
                            .localRotation = nextRotation,
                            .localScale = nextScale
                        });
                        key.localScale = nextScale;
                    });
                }

                bool visible = effectiveVisible;
                if (ImGui::Checkbox("Visible", &visible))
                {
                    const std::string partId = selectedPart->partId;
                    const float time = _previewTimeSeconds;
                    const bool nextVisible = visible;
                    apply_clip_edit("Key visibility", [partId, time, nextVisible](VoxelAnimationClipAsset& clip)
                    {
                        VoxelAnimationPartTrack& track = ensure_part_track(clip, partId);
                        VoxelAnimationVisibilityKeyframe& key = ensure_visibility_key(track, time, nextVisible);
                        key.visible = nextVisible;
                    });
                }

                const char* bindingLabel = effectiveBindingStateId.empty() ? "<default>" : effectiveBindingStateId.c_str();
                if (ImGui::BeginCombo("Binding State", bindingLabel))
                {
                    const bool usingDefaultState = effectiveBindingStateId.empty() || effectiveBindingStateId == selectedPart->defaultStateId;
                    if (ImGui::Selectable("<default>", usingDefaultState))
                    {
                        const std::string partId = selectedPart->partId;
                        const float time = _previewTimeSeconds;
                        apply_clip_edit("Clear binding state key", [partId, time](VoxelAnimationClipAsset& clip)
                        {
                            if (VoxelAnimationBindingTrack* track = clip.find_binding_track(partId); track != nullptr)
                            {
                                std::erase_if(track->keys, [time](const VoxelAnimationBindingStateKeyframe& key)
                                {
                                    return keyframe_time_matches(key.timeSeconds, time);
                                });
                            }
                            prune_empty_tracks(clip);
                        });
                    }

                    for (const VoxelAssemblyBindingState& bindingState : selectedPart->bindingStates)
                    {
                        const bool selected = bindingState.stateId == effectiveBindingStateId;
                        if (ImGui::Selectable(bindingState.stateId.c_str(), selected))
                        {
                            const std::string partId = selectedPart->partId;
                            const std::string nextStateId = bindingState.stateId;
                            const float time = _previewTimeSeconds;
                            apply_clip_edit("Key binding state", [partId, nextStateId, time](VoxelAnimationClipAsset& clip)
                            {
                                VoxelAnimationBindingTrack& track = ensure_binding_track(clip, partId);
                                VoxelAnimationBindingStateKeyframe& key = ensure_binding_key(track, time, nextStateId);
                                key.stateId = nextStateId;
                            });
                        }
                    }

                    ImGui::EndCombo();
                }

                ImGui::SeparatorText("Track Summary");
                ImGui::Text("Transform Keys: %d", partTrack != nullptr ? static_cast<int>(partTrack->transformKeys.size()) : 0);
                if (partTrack != nullptr)
                {
                    for (size_t keyIndex = 0; keyIndex < partTrack->transformKeys.size(); ++keyIndex)
                    {
                        const VoxelAnimationTransformKeyframe& key = partTrack->transformKeys[keyIndex];
                        ImGui::PushID(static_cast<int>(keyIndex));
                        const std::string label = std::format("T {:.3f}s", key.timeSeconds);
                        if (ImGui::Selectable(label.c_str(), keyframe_time_matches(_previewTimeSeconds, key.timeSeconds)))
                        {
                            _previewTimeSeconds = key.timeSeconds;
                            _previewDirty = true;
                        }
                        ImGui::PopID();
                    }
                }

                ImGui::Text("Visibility Keys: %d", partTrack != nullptr ? static_cast<int>(partTrack->visibilityKeys.size()) : 0);
                if (partTrack != nullptr)
                {
                    for (size_t keyIndex = 0; keyIndex < partTrack->visibilityKeys.size(); ++keyIndex)
                    {
                        const VoxelAnimationVisibilityKeyframe& key = partTrack->visibilityKeys[keyIndex];
                        ImGui::PushID(std::format("Visibility{}", keyIndex).c_str());
                        const std::string label = std::format("V {:.3f}s -> {}", key.timeSeconds, key.visible ? "Visible" : "Hidden");
                        if (ImGui::Selectable(label.c_str(), keyframe_time_matches(_previewTimeSeconds, key.timeSeconds)))
                        {
                            _previewTimeSeconds = key.timeSeconds;
                            _previewDirty = true;
                        }
                        ImGui::PopID();
                    }
                }

                ImGui::Text("Binding Keys: %d", bindingTrack != nullptr ? static_cast<int>(bindingTrack->keys.size()) : 0);
                if (bindingTrack != nullptr)
                {
                    for (size_t keyIndex = 0; keyIndex < bindingTrack->keys.size(); ++keyIndex)
                    {
                        const VoxelAnimationBindingStateKeyframe& key = bindingTrack->keys[keyIndex];
                        ImGui::PushID(std::format("Binding{}", keyIndex).c_str());
                        const std::string label = std::format("B {:.3f}s -> {}", key.timeSeconds, key.stateId);
                        if (ImGui::Selectable(label.c_str(), keyframe_time_matches(_previewTimeSeconds, key.timeSeconds)))
                        {
                            _previewTimeSeconds = key.timeSeconds;
                            _previewDirty = true;
                        }
                        ImGui::PopID();
                    }
                }
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
