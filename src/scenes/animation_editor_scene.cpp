#include "animation_editor_scene.h"

#include <algorithm>
#include <cctype>
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
#include "imnodes.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "orbit_orientation_gizmo.h"
#include "editor_preview_primitives.h"
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
    constexpr const char* ControllerConditionOpLabels = "Greater\0Less\0Greater Equal\0Less Equal\0Equal\0Not Equal\0Is True\0Is False\0Triggered\0";
    constexpr const char* ControllerNodeTypeLabels = "Clip Player\0Blend Space 2D\0";
    constexpr const char* ControllerBlendModeLabels = "Override\0Additive\0";
    constexpr const char* ControllerRootMotionLabels = "Ignore\0Extract Planar\0Extract Full\0";

    glm::vec3 euler_degrees_from_quat(const glm::quat& rotation)
    {
        return glm::degrees(glm::eulerAngles(rotation));
    }

    [[nodiscard]] glm::quat quat_from_euler_degrees(const glm::vec3& rotationDegrees)
    {
        return glm::quat(glm::radians(rotationDegrees));
    }

    [[nodiscard]] glm::quat normalized_quat(glm::quat value)
    {
        if (glm::length(value) <= 0.00001f)
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        return glm::normalize(value);
    }

    [[nodiscard]] glm::vec3 compose_binding_relative_position(
        const VoxelAssemblyBindingState* const bindingState,
        const VoxelAssemblyPosePart* const posePart)
    {
        const glm::vec3 base = bindingState != nullptr ? bindingState->localPositionOffset : glm::vec3(0.0f);
        return posePart != nullptr && posePart->localPosition.has_value()
            ? (base + posePart->localPosition.value())
            : base;
    }

    [[nodiscard]] glm::quat compose_binding_relative_rotation(
        const VoxelAssemblyBindingState* const bindingState,
        const VoxelAssemblyPosePart* const posePart)
    {
        const glm::quat base = bindingState != nullptr
            ? bindingState->localRotationOffset
            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return posePart != nullptr && posePart->localRotation.has_value()
            ? normalized_quat(base * posePart->localRotation.value())
            : base;
    }

    [[nodiscard]] glm::vec3 compose_binding_relative_scale(
        const VoxelAssemblyBindingState* const bindingState,
        const VoxelAssemblyPosePart* const posePart)
    {
        const glm::vec3 base = bindingState != nullptr ? bindingState->localScale : glm::vec3(1.0f);
        return posePart != nullptr && posePart->localScale.has_value()
            ? (base * posePart->localScale.value())
            : base;
    }

    [[nodiscard]] glm::vec3 relative_position_from_binding(
        const VoxelAssemblyBindingState* const bindingState,
        const glm::vec3& effectivePosition)
    {
        const glm::vec3 base = bindingState != nullptr ? bindingState->localPositionOffset : glm::vec3(0.0f);
        return effectivePosition - base;
    }

    [[nodiscard]] glm::quat relative_rotation_from_binding(
        const VoxelAssemblyBindingState* const bindingState,
        const glm::quat& effectiveRotation)
    {
        const glm::quat base = bindingState != nullptr
            ? bindingState->localRotationOffset
            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return normalized_quat(glm::inverse(base) * effectiveRotation);
    }

    [[nodiscard]] glm::vec3 relative_scale_from_binding(
        const VoxelAssemblyBindingState* const bindingState,
        const glm::vec3& effectiveScale)
    {
        const glm::vec3 base = bindingState != nullptr ? bindingState->localScale : glm::vec3(1.0f);
        glm::vec3 result(1.0f);
        result.x = std::abs(base.x) > 0.00001f ? effectiveScale.x / base.x : 1.0f;
        result.y = std::abs(base.y) > 0.00001f ? effectiveScale.y / base.y : 1.0f;
        result.z = std::abs(base.z) > 0.00001f ? effectiveScale.z / base.z : 1.0f;
        return result;
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
        int sourceIndex{-1};
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
            const int frameMax,
            int* selectedEntry) :
            _entries(std::move(entries)),
            _frameMin(frameMin),
            _frameMax(std::max(frameMax, frameMin + 1)),
            _selectedEntry(selectedEntry)
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
            if (_selectedEntry != nullptr &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                rc.Contains(ImGui::GetIO().MousePos))
            {
                *_selectedEntry = index;
            }

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
        int* _selectedEntry{nullptr};
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
            (void)maxIt;
            const int anchor = *minIt;
            return { anchor, anchor };
        };

        for (size_t trackIndex = 0; trackIndex < clip.partTracks.size(); ++trackIndex)
        {
            const VoxelAnimationPartTrack& track = clip.partTracks[trackIndex];
            if (!track.transformKeys.empty())
            {
                ClipSequencerEntry entry{};
                entry.laneType = ClipSequencerLaneType::Transform;
                entry.label = std::format("{} / Transform", track.partId);
                entry.partId = track.partId;
                entry.sourceIndex = static_cast<int>(trackIndex);
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
                entry.sourceIndex = static_cast<int>(trackIndex);
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

        for (size_t trackIndex = 0; trackIndex < clip.bindingTracks.size(); ++trackIndex)
        {
            const VoxelAnimationBindingTrack& track = clip.bindingTracks[trackIndex];
            if (track.keys.empty())
            {
                continue;
            }

            ClipSequencerEntry entry{};
            entry.laneType = ClipSequencerLaneType::Binding;
            entry.label = std::format("{} / Binding", track.partId);
            entry.partId = track.partId;
            entry.sourceIndex = static_cast<int>(trackIndex);
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

        for (size_t trackIndex = 0; trackIndex < clip.eventTracks.size(); ++trackIndex)
        {
            const VoxelAnimationEventTrack& track = clip.eventTracks[trackIndex];
            if (track.events.empty())
            {
                continue;
            }

            ClipSequencerEntry entry{};
            entry.laneType = ClipSequencerLaneType::Event;
            entry.label = std::format("{} / Events", track.trackId.empty() ? "event_track" : track.trackId);
            entry.trackId = track.trackId;
            entry.sourceIndex = static_cast<int>(trackIndex);
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

    [[nodiscard]] std::optional<ClipSequencerEntry> selected_clip_sequencer_entry(
        const VoxelAnimationClipAsset& clip,
        const int selectedEntry)
    {
        const std::vector<ClipSequencerEntry> entries = build_clip_sequencer_entries(clip);
        if (selectedEntry < 0 || selectedEntry >= static_cast<int>(entries.size()))
        {
            return std::nullopt;
        }
        return entries[static_cast<size_t>(selectedEntry)];
    }

    [[nodiscard]] int lane_key_count(const VoxelAnimationClipAsset& clip, const ClipSequencerEntry& entry)
    {
        switch (entry.laneType)
        {
        case ClipSequencerLaneType::Transform:
            return entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size())
                ? static_cast<int>(clip.partTracks[static_cast<size_t>(entry.sourceIndex)].transformKeys.size())
                : 0;
        case ClipSequencerLaneType::Visibility:
            return entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size())
                ? static_cast<int>(clip.partTracks[static_cast<size_t>(entry.sourceIndex)].visibilityKeys.size())
                : 0;
        case ClipSequencerLaneType::Binding:
            return entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.bindingTracks.size())
                ? static_cast<int>(clip.bindingTracks[static_cast<size_t>(entry.sourceIndex)].keys.size())
                : 0;
        case ClipSequencerLaneType::Event:
            return entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.eventTracks.size())
                ? static_cast<int>(clip.eventTracks[static_cast<size_t>(entry.sourceIndex)].events.size())
                : 0;
        }

        return 0;
    }

    [[nodiscard]] int lane_key_frame(
        const VoxelAnimationClipAsset& clip,
        const ClipSequencerEntry& entry,
        const int keyIndex)
    {
        if (keyIndex < 0)
        {
            return 0;
        }

        const float frameRate = std::max(clip.frameRateHint, 1.0f);
        switch (entry.laneType)
        {
        case ClipSequencerLaneType::Transform:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size()))
            {
                const auto& keys = clip.partTracks[static_cast<size_t>(entry.sourceIndex)].transformKeys;
                if (keyIndex < static_cast<int>(keys.size()))
                {
                    return frame_from_time(keys[static_cast<size_t>(keyIndex)].timeSeconds, frameRate);
                }
            }
            break;
        case ClipSequencerLaneType::Visibility:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size()))
            {
                const auto& keys = clip.partTracks[static_cast<size_t>(entry.sourceIndex)].visibilityKeys;
                if (keyIndex < static_cast<int>(keys.size()))
                {
                    return frame_from_time(keys[static_cast<size_t>(keyIndex)].timeSeconds, frameRate);
                }
            }
            break;
        case ClipSequencerLaneType::Binding:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.bindingTracks.size()))
            {
                const auto& keys = clip.bindingTracks[static_cast<size_t>(entry.sourceIndex)].keys;
                if (keyIndex < static_cast<int>(keys.size()))
                {
                    return frame_from_time(keys[static_cast<size_t>(keyIndex)].timeSeconds, frameRate);
                }
            }
            break;
        case ClipSequencerLaneType::Event:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.eventTracks.size()))
            {
                const auto& keys = clip.eventTracks[static_cast<size_t>(entry.sourceIndex)].events;
                if (keyIndex < static_cast<int>(keys.size()))
                {
                    return frame_from_time(keys[static_cast<size_t>(keyIndex)].timeSeconds, frameRate);
                }
            }
            break;
        }

        return 0;
    }

    bool move_lane_key(
        VoxelAnimationClipAsset& clip,
        const ClipSequencerEntry& entry,
        const int keyIndex,
        const float nextTimeSeconds)
    {
        const float clampedTime = std::clamp(nextTimeSeconds, 0.0f, clip.durationSeconds);
        switch (entry.laneType)
        {
        case ClipSequencerLaneType::Transform:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size()))
            {
                auto& keys = clip.partTracks[static_cast<size_t>(entry.sourceIndex)].transformKeys;
                if (keyIndex >= 0 && keyIndex < static_cast<int>(keys.size()))
                {
                    keys[static_cast<size_t>(keyIndex)].timeSeconds = clampedTime;
                    sort_keyframes(keys);
                    return true;
                }
            }
            break;
        case ClipSequencerLaneType::Visibility:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size()))
            {
                auto& keys = clip.partTracks[static_cast<size_t>(entry.sourceIndex)].visibilityKeys;
                if (keyIndex >= 0 && keyIndex < static_cast<int>(keys.size()))
                {
                    keys[static_cast<size_t>(keyIndex)].timeSeconds = clampedTime;
                    sort_keyframes(keys);
                    return true;
                }
            }
            break;
        case ClipSequencerLaneType::Binding:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.bindingTracks.size()))
            {
                auto& keys = clip.bindingTracks[static_cast<size_t>(entry.sourceIndex)].keys;
                if (keyIndex >= 0 && keyIndex < static_cast<int>(keys.size()))
                {
                    keys[static_cast<size_t>(keyIndex)].timeSeconds = clampedTime;
                    sort_keyframes(keys);
                    return true;
                }
            }
            break;
        case ClipSequencerLaneType::Event:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.eventTracks.size()))
            {
                auto& keys = clip.eventTracks[static_cast<size_t>(entry.sourceIndex)].events;
                if (keyIndex >= 0 && keyIndex < static_cast<int>(keys.size()))
                {
                    keys[static_cast<size_t>(keyIndex)].timeSeconds = clampedTime;
                    sort_keyframes(keys);
                    return true;
                }
            }
            break;
        }

        return false;
    }

    bool delete_lane_key(
        VoxelAnimationClipAsset& clip,
        const ClipSequencerEntry& entry,
        const int keyIndex)
    {
        switch (entry.laneType)
        {
        case ClipSequencerLaneType::Transform:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size()))
            {
                auto& keys = clip.partTracks[static_cast<size_t>(entry.sourceIndex)].transformKeys;
                if (keyIndex >= 0 && keyIndex < static_cast<int>(keys.size()))
                {
                    keys.erase(keys.begin() + keyIndex);
                    prune_empty_tracks(clip);
                    return true;
                }
            }
            break;
        case ClipSequencerLaneType::Visibility:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size()))
            {
                auto& keys = clip.partTracks[static_cast<size_t>(entry.sourceIndex)].visibilityKeys;
                if (keyIndex >= 0 && keyIndex < static_cast<int>(keys.size()))
                {
                    keys.erase(keys.begin() + keyIndex);
                    prune_empty_tracks(clip);
                    return true;
                }
            }
            break;
        case ClipSequencerLaneType::Binding:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.bindingTracks.size()))
            {
                auto& keys = clip.bindingTracks[static_cast<size_t>(entry.sourceIndex)].keys;
                if (keyIndex >= 0 && keyIndex < static_cast<int>(keys.size()))
                {
                    keys.erase(keys.begin() + keyIndex);
                    prune_empty_tracks(clip);
                    return true;
                }
            }
            break;
        case ClipSequencerLaneType::Event:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.eventTracks.size()))
            {
                auto& keys = clip.eventTracks[static_cast<size_t>(entry.sourceIndex)].events;
                if (keyIndex >= 0 && keyIndex < static_cast<int>(keys.size()))
                {
                    keys.erase(keys.begin() + keyIndex);
                    std::erase_if(clip.eventTracks, [](const VoxelAnimationEventTrack& track)
                    {
                        return track.events.empty();
                    });
                    return true;
                }
            }
            break;
        }

        return false;
    }

    bool delete_lane(
        VoxelAnimationClipAsset& clip,
        const ClipSequencerEntry& entry)
    {
        switch (entry.laneType)
        {
        case ClipSequencerLaneType::Transform:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size()))
            {
                clip.partTracks[static_cast<size_t>(entry.sourceIndex)].transformKeys.clear();
                prune_empty_tracks(clip);
                return true;
            }
            break;
        case ClipSequencerLaneType::Visibility:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.partTracks.size()))
            {
                clip.partTracks[static_cast<size_t>(entry.sourceIndex)].visibilityKeys.clear();
                prune_empty_tracks(clip);
                return true;
            }
            break;
        case ClipSequencerLaneType::Binding:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.bindingTracks.size()))
            {
                clip.bindingTracks.erase(clip.bindingTracks.begin() + entry.sourceIndex);
                return true;
            }
            break;
        case ClipSequencerLaneType::Event:
            if (entry.sourceIndex >= 0 && entry.sourceIndex < static_cast<int>(clip.eventTracks.size()))
            {
                clip.eventTracks.erase(clip.eventTracks.begin() + entry.sourceIndex);
                return true;
            }
            break;
        }

        return false;
    }

    [[nodiscard]] std::string sanitize_identifier(const std::string_view rawValue, const std::string_view fallback)
    {
        std::string result{};
        result.reserve(rawValue.size());
        for (const char ch : rawValue)
        {
            if ((ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9'))
            {
                result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            else if (ch == '_' || ch == '-' || ch == ' ')
            {
                result.push_back('_');
            }
        }

        if (result.empty())
        {
            result = std::string(fallback);
        }
        return result;
    }

    template <typename TPredicate>
    [[nodiscard]] std::string make_unique_identifier(
        const std::string_view base,
        const std::string_view fallback,
        const TPredicate& exists)
    {
        const std::string root = sanitize_identifier(base, fallback);
        if (!exists(root))
        {
            return root;
        }

        for (int index = 1; index < 1024; ++index)
        {
            const std::string candidate = std::format("{}_{}", root, index);
            if (!exists(candidate))
            {
                return candidate;
            }
        }

        return std::format("{}_{}", root, 1024);
    }

    [[nodiscard]] uint32_t stable_hash_u32(const std::string_view value)
    {
        uint32_t hash = 2166136261u;
        for (const unsigned char ch : value)
        {
            hash ^= static_cast<uint32_t>(ch);
            hash *= 16777619u;
        }
        return hash;
    }

    [[nodiscard]] int stable_imnodes_id(
        const std::string_view kind,
        const std::string_view layerId,
        const std::string_view objectId,
        const std::string_view extra = {})
    {
        const std::string combined = std::format("{}|{}|{}|{}", kind, layerId, objectId, extra);
        return static_cast<int>(stable_hash_u32(combined) & 0x7fffffffu);
    }

    [[nodiscard]] const char* parameter_type_label(const VoxelAnimationParameterType type)
    {
        switch (type)
        {
        case VoxelAnimationParameterType::Bool:
            return "Bool";
        case VoxelAnimationParameterType::Trigger:
            return "Trigger";
        case VoxelAnimationParameterType::Float:
        default:
            return "Float";
        }
    }

    [[nodiscard]] const char* condition_op_label(const VoxelAnimationConditionOp op)
    {
        switch (op)
        {
        case VoxelAnimationConditionOp::Greater:
            return "Greater";
        case VoxelAnimationConditionOp::Less:
            return "Less";
        case VoxelAnimationConditionOp::LessEqual:
            return "Less Equal";
        case VoxelAnimationConditionOp::Equal:
            return "Equal";
        case VoxelAnimationConditionOp::NotEqual:
            return "Not Equal";
        case VoxelAnimationConditionOp::IsTrue:
            return "Is True";
        case VoxelAnimationConditionOp::IsFalse:
            return "Is False";
        case VoxelAnimationConditionOp::Triggered:
            return "Triggered";
        case VoxelAnimationConditionOp::GreaterEqual:
        default:
            return "Greater Equal";
        }
    }

    [[nodiscard]] std::string transition_selection_key(
        const std::string_view layerId,
        const std::string_view sourceStateId,
        const std::string_view targetStateId)
    {
        return std::format("{}|{}|{}", layerId, sourceStateId, targetStateId);
    }

    [[nodiscard]] std::pair<std::string, std::string> transition_endpoints_from_key(
        const std::string_view selectionKey)
    {
        const size_t first = selectionKey.find('|');
        if (first == std::string_view::npos)
        {
            return {};
        }
        const size_t second = selectionKey.find('|', first + 1);
        if (second == std::string_view::npos)
        {
            return {};
        }
        return {
            std::string(selectionKey.substr(first + 1, second - first - 1)),
            std::string(selectionKey.substr(second + 1))
        };
    }

    [[nodiscard]] const VoxelAnimationTransitionDefinition* find_transition(
        const VoxelAnimationLayerDefinition& layer,
        const std::string_view sourceStateId,
        const std::string_view targetStateId)
    {
        const auto it = std::ranges::find_if(layer.transitions, [&](const VoxelAnimationTransitionDefinition& transition)
        {
            return transition.sourceStateId == sourceStateId && transition.targetStateId == targetStateId;
        });
        return it != layer.transitions.end() ? &(*it) : nullptr;
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

    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImNodes::CreateContext();
        _controllerNodeEditor = ImNodes::EditorContextCreate();
    }

    new_clip();
    new_controller();
    refresh_asset_lists();
    build_pipelines();
    update_camera();
    update_uniform_buffers();
}

AnimationEditorScene::~AnimationEditorScene()
{
    if (_controllerNodeEditor != nullptr)
    {
        ImNodes::EditorContextFree(_controllerNodeEditor);
        _controllerNodeEditor = nullptr;
    }
    if (ImNodes::GetCurrentContext() != nullptr)
    {
        ImNodes::DestroyContext();
    }

    _previewRegistry.clear(_renderState);
    release_selection_meshes();
}

void AnimationEditorScene::update_buffers()
{
    sync_preview();
    _previewRegistry.sync(*_services.meshManager, *_services.materialManager, AnimationEditorMaterialScope, _renderState);
    if (_showSelectedPartBounds &&
        !_selectedPartId.empty() &&
        (!_selectedPartBoundsHandle.has_value() || !_renderState.transparentObjects.live(_selectedPartBoundsHandle.value())))
    {
        _selectionOverlayDirty = true;
    }
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
            _controllerPreviewPendingDeltaSeconds += deltaTime;
        }
        _previewDirty = true;
        _selectionOverlayDirty = true;
    }

    update_camera();
}

void AnimationEditorScene::handle_input(const SDL_Event& event)
{
    if (event.type == SDL_KEYDOWN &&
        event.key.repeat == 0 &&
        (event.key.keysym.sym == SDLK_DELETE || event.key.keysym.sym == SDLK_BACKSPACE))
    {
        const bool wantsTextInput =
            USE_IMGUI &&
            ImGui::GetCurrentContext() != nullptr &&
            ImGui::GetIO().WantTextInput;
        if (!wantsTextInput && !ImGuizmo::IsUsing())
        {
            if (try_delete_current_selection())
            {
                return;
            }
        }
    }

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
        _orbitCamera.yawDegrees += static_cast<float>(event.motion.xrel) * 0.18f;
        _orbitCamera.pitchDegrees -= static_cast<float>(event.motion.yrel) * 0.18f;
        return;
    }

    if (event.type == SDL_MOUSEWHEEL)
    {
        _orbitCamera.distance = std::clamp(
            _orbitCamera.distance - (static_cast<float>(event.wheel.y) * 0.25f),
            _orbitCamera.minDistance,
            _orbitCamera.maxDistance);
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
    editor::update_orbit_camera(*_camera, orbit_target(), _orbitCamera);
}

void AnimationEditorScene::draw_orientation_gizmo()
{
    (void)editor::draw_orbit_orientation_gizmo(*_camera, orbit_target(), _orbitCamera);
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
        _sequencerSelectedEntry = -1;
        _sequencerSelectedKeyIndex = -1;
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
    _selectedControllerLayerId.clear();
    _selectedControllerStateId.clear();
    _selectedControllerTransitionKey.clear();
    _initializedControllerNodeIds.clear();
    reset_controller_preview_state(false);
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
        _selectedControllerLayerId = !_controller.layers.empty() ? _controller.layers.front().layerId : std::string{};
        _selectedControllerStateId.clear();
        _selectedControllerTransitionKey.clear();
        _initializedControllerNodeIds.clear();
        reset_controller_preview_state(false);
        _statusMessage = std::format("Loaded controller '{}'.", _controller.assetId);
    }
}

void AnimationEditorScene::reset_controller_preview_state(const bool preserveParameters)
{
    _previewAnimation.controllerAssetId.clear();
    _previewAnimation.layerStates.clear();
    _previewAnimation.currentPose.clear();
    _previewAnimation.pendingEvents.clear();
    _previewAnimation.rootMotion = {};
    if (!preserveParameters)
    {
        _previewAnimation.floatParameters.clear();
        _previewAnimation.boolParameters.clear();
        _previewAnimation.triggerParameters.clear();
    }
    _controllerPreviewPendingDeltaSeconds = 0.0f;
    _previewTimeSeconds = 0.0f;
}

void AnimationEditorScene::sync_controller_preview_parameters()
{
    std::unordered_set<std::string> floatIds{};
    std::unordered_set<std::string> boolIds{};
    std::unordered_set<std::string> triggerIds{};
    floatIds.reserve(_controller.parameters.size());
    boolIds.reserve(_controller.parameters.size());
    triggerIds.reserve(_controller.parameters.size());

    for (const VoxelAnimationParameterDefinition& parameter : _controller.parameters)
    {
        if (parameter.parameterId.empty())
        {
            continue;
        }

        switch (parameter.type)
        {
        case VoxelAnimationParameterType::Bool:
            boolIds.insert(parameter.parameterId);
            _previewAnimation.floatParameters.erase(parameter.parameterId);
            _previewAnimation.triggerParameters.erase(parameter.parameterId);
            if (!_previewAnimation.boolParameters.contains(parameter.parameterId))
            {
                _previewAnimation.boolParameters.insert_or_assign(parameter.parameterId, parameter.defaultBoolValue);
            }
            break;
        case VoxelAnimationParameterType::Trigger:
            triggerIds.insert(parameter.parameterId);
            _previewAnimation.floatParameters.erase(parameter.parameterId);
            _previewAnimation.boolParameters.erase(parameter.parameterId);
            if (!_previewAnimation.triggerParameters.contains(parameter.parameterId))
            {
                _previewAnimation.triggerParameters.insert_or_assign(parameter.parameterId, false);
            }
            break;
        case VoxelAnimationParameterType::Float:
        default:
            floatIds.insert(parameter.parameterId);
            _previewAnimation.boolParameters.erase(parameter.parameterId);
            _previewAnimation.triggerParameters.erase(parameter.parameterId);
            if (!_previewAnimation.floatParameters.contains(parameter.parameterId))
            {
                _previewAnimation.floatParameters.insert_or_assign(parameter.parameterId, parameter.defaultFloatValue);
            }
            break;
        }
    }

    std::erase_if(_previewAnimation.floatParameters, [&](const auto& entry)
    {
        return !floatIds.contains(entry.first);
    });
    std::erase_if(_previewAnimation.boolParameters, [&](const auto& entry)
    {
        return !boolIds.contains(entry.first);
    });
    std::erase_if(_previewAnimation.triggerParameters, [&](const auto& entry)
    {
        return !triggerIds.contains(entry.first);
    });
}

AnimationEditorScene::DeleteSelection AnimationEditorScene::current_delete_selection() const
{
    if (_mode == EditorMode::Clip)
    {
        if (_sequencerSelectedEntry >= 0)
        {
            return DeleteSelection{
                .kind = _sequencerSelectedKeyIndex >= 0
                    ? DeleteSelectionKind::ClipTimelineKey
                    : DeleteSelectionKind::ClipTimelineLane
            };
        }
        return {};
    }

    if (!_selectedControllerTransitionKey.empty())
    {
        return DeleteSelection{ .kind = DeleteSelectionKind::ControllerTransition };
    }
    if (!_selectedControllerStateId.empty())
    {
        return DeleteSelection{ .kind = DeleteSelectionKind::ControllerState };
    }
    if (!_selectedControllerLayerId.empty())
    {
        return DeleteSelection{ .kind = DeleteSelectionKind::ControllerLayer };
    }
    return {};
}

bool AnimationEditorScene::try_delete_selection(const DeleteSelection selection)
{
    switch (selection.kind)
    {
    case DeleteSelectionKind::ClipTimelineKey:
    {
        const std::optional<ClipSequencerEntry> selectedEntry = selected_clip_sequencer_entry(_clip, _sequencerSelectedEntry);
        if (!selectedEntry.has_value() || _sequencerSelectedKeyIndex < 0)
        {
            return false;
        }

        const int keyIndex = _sequencerSelectedKeyIndex;
        const ClipSequencerEntry laneEntry = selectedEntry.value();
        apply_clip_edit("Delete timeline key", [laneEntry, keyIndex](VoxelAnimationClipAsset& clip)
        {
            delete_lane_key(clip, laneEntry, keyIndex);
        });
        _sequencerSelectedKeyIndex = -1;
        return true;
    }
    case DeleteSelectionKind::ClipTimelineLane:
    {
        const std::optional<ClipSequencerEntry> selectedEntry = selected_clip_sequencer_entry(_clip, _sequencerSelectedEntry);
        if (!selectedEntry.has_value())
        {
            return false;
        }

        const ClipSequencerEntry laneEntry = selectedEntry.value();
        apply_clip_edit("Delete timeline lane", [laneEntry](VoxelAnimationClipAsset& clip)
        {
            delete_lane(clip, laneEntry);
        });
        _sequencerSelectedEntry = -1;
        _sequencerSelectedKeyIndex = -1;
        return true;
    }
    case DeleteSelectionKind::ControllerState:
    {
        if (_selectedControllerLayerId.empty() || _selectedControllerStateId.empty())
        {
            return false;
        }

        const std::string selectedLayerId = _selectedControllerLayerId;
        const std::string stateId = _selectedControllerStateId;
        apply_controller_edit("Delete controller state", [selectedLayerId, stateId](VoxelAnimationControllerAsset& controller)
        {
            VoxelAnimationLayerDefinition* const layer = controller.find_layer(selectedLayerId);
            if (layer == nullptr)
            {
                return;
            }

            std::erase_if(layer->states, [&](const VoxelAnimationStateDefinition& state)
            {
                return state.stateId == stateId;
            });
            std::erase_if(layer->transitions, [&](const VoxelAnimationTransitionDefinition& transition)
            {
                return transition.sourceStateId == stateId || transition.targetStateId == stateId;
            });
            if (layer->entryStateId == stateId)
            {
                layer->entryStateId = !layer->states.empty() ? layer->states.front().stateId : std::string{};
            }
        });
        _selectedControllerStateId.clear();
        _selectedControllerTransitionKey.clear();
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        return true;
    }
    case DeleteSelectionKind::ControllerTransition:
    {
        if (_selectedControllerLayerId.empty() || _selectedControllerTransitionKey.empty())
        {
            return false;
        }

        const std::string selectedLayerId = _selectedControllerLayerId;
        const auto [sourceStateId, targetStateId] = transition_endpoints_from_key(_selectedControllerTransitionKey);
        apply_controller_edit("Delete controller transition", [selectedLayerId, sourceStateId, targetStateId](VoxelAnimationControllerAsset& controller)
        {
            VoxelAnimationLayerDefinition* const layer = controller.find_layer(selectedLayerId);
            if (layer == nullptr)
            {
                return;
            }

            std::erase_if(layer->transitions, [&](const VoxelAnimationTransitionDefinition& transition)
            {
                return transition.sourceStateId == sourceStateId && transition.targetStateId == targetStateId;
            });
        });
        _selectedControllerTransitionKey.clear();
        ImNodes::ClearLinkSelection();
        return true;
    }
    case DeleteSelectionKind::ControllerLayer:
    {
        if (_selectedControllerLayerId.empty())
        {
            return false;
        }

        const std::string layerId = _selectedControllerLayerId;
        apply_controller_edit("Delete controller layer", [layerId](VoxelAnimationControllerAsset& controller)
        {
            std::erase_if(controller.layers, [&](const VoxelAnimationLayerDefinition& layer)
            {
                return layer.layerId == layerId;
            });
        });
        _selectedControllerLayerId = !_controller.layers.empty() ? _controller.layers.front().layerId : std::string{};
        _selectedControllerStateId.clear();
        _selectedControllerTransitionKey.clear();
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        return true;
    }
    case DeleteSelectionKind::None:
    default:
        return false;
    }
}

bool AnimationEditorScene::try_delete_current_selection()
{
    return try_delete_selection(current_delete_selection());
}

bool AnimationEditorScene::delete_selected_clip_asset()
{
    if (_clip.assetId.empty())
    {
        return false;
    }

    if (_clipRepository.remove(_clip.assetId))
    {
        _clipAssetManager.clear();
        refresh_asset_lists();
        new_clip();
        _statusMessage = "Deleted clip.";
        return true;
    }

    _statusMessage = "Clip delete skipped or file was missing.";
    return false;
}

bool AnimationEditorScene::delete_selected_controller_asset()
{
    if (_controller.assetId.empty())
    {
        return false;
    }

    if (_controllerRepository.remove(_controller.assetId))
    {
        _controllerAssetManager.clear();
        refresh_asset_lists();
        new_controller();
        _statusMessage = "Deleted controller.";
        return true;
    }

    _statusMessage = "Controller delete skipped or file was missing.";
    return false;
}

void AnimationEditorScene::apply_clip_edit(
    const std::string_view description,
    const std::function<void(VoxelAnimationClipAsset&)>& edit)
{
    (void)_clipSession.apply(description, edit, [this]()
    {
        _previewDirty = true;
    });
}

void AnimationEditorScene::apply_controller_edit(
    const std::string_view description,
    const std::function<void(VoxelAnimationControllerAsset&)>& edit)
{
    (void)_controllerSession.apply(description, edit, [this]()
    {
        if (_selectedControllerLayerId.empty() && !_controller.layers.empty())
        {
            _selectedControllerLayerId = _controller.layers.front().layerId;
        }
        if (!_selectedControllerLayerId.empty() && _controller.find_layer(_selectedControllerLayerId) == nullptr)
        {
            _selectedControllerLayerId = !_controller.layers.empty() ? _controller.layers.front().layerId : std::string{};
        }
        if (!_selectedControllerLayerId.empty())
        {
            const VoxelAnimationLayerDefinition* const selectedLayer = _controller.find_layer(_selectedControllerLayerId);
            if (selectedLayer != nullptr &&
                !_selectedControllerStateId.empty() &&
                selectedLayer->find_state(_selectedControllerStateId) == nullptr)
            {
                _selectedControllerStateId.clear();
            }
        }
        reset_controller_preview_state(true);
        _previewDirty = true;
    });
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
        sync_controller_preview_parameters();
        tick_voxel_animation_component(
            _previewAnimation,
            _previewAssembly,
            *assembly,
            _controller,
            _clipAssetManager,
            _controllerPreviewPendingDeltaSeconds);
        _controllerPreviewPendingDeltaSeconds = 0.0f;
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

    if (!_autoCenterPreview)
    {
        const VoxelRenderInstance* rootInstance = nullptr;
        if (!assembly->rootPartId.empty())
        {
            const auto rootIt = _previewParts.find(assembly->rootPartId);
            if (rootIt != _previewParts.end())
            {
                rootInstance = &rootIt->second;
            }
        }

        if (rootInstance != nullptr)
        {
            _previewOrbitTarget = rootInstance->asset != nullptr
                ? rootInstance->world_point_from_asset_local(rootInstance->asset->model.pivot)
                : rootInstance->position;
        }
        else if (centerCount > 0)
        {
            _previewOrbitTarget = centerAccumulator / static_cast<float>(centerCount);
        }
    }
    else if (centerCount > 0)
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

    if (!_showSelectedPartBounds || _selectedPartId.empty())
    {
        release_selection_meshes();
        return;
    }

    const auto previewIt = _previewParts.find(_selectedPartId);
    if (previewIt == _previewParts.end())
    {
        release_selection_meshes();
        return;
    }

    (void)editor::sync_bounds_overlay_for_instance(
        _selectedPartBoundsMesh,
        _selectedPartBoundsHandle,
        _renderState,
        _services,
        AnimationEditorMaterialScope,
        &previewIt->second,
        true,
        glm::vec3(0.28f, 0.92f, 1.0f));
}

void AnimationEditorScene::release_selection_meshes()
{
    editor::clear_bounds_overlay(_selectedPartBoundsMesh, _selectedPartBoundsHandle, _renderState);
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

    if (editor::begin_main_viewport_gizmo() == nullptr)
    {
        return;
    }

    const VoxelRenderInstance& instance = previewIt->second;
    glm::mat4 worldMatrix = editor::pivot_transform_matrix(instance);
    const glm::mat4 parentBasis = editor::parent_basis_matrix(_previewParts, *bindingState);

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
        const glm::vec3 nextPositionOffset = relative_position_from_binding(bindingState, nextPosition);
        const glm::quat nextRotationOffset = relative_rotation_from_binding(bindingState, nextRotation);
        const glm::vec3 nextScaleOffset = relative_scale_from_binding(bindingState, nextScale);
        const std::string partId = selectedPart->partId;
        const float time = _previewTimeSeconds;

        apply_clip_edit("Manipulate transform gizmo", [partId, time, nextPositionOffset, nextRotationOffset, nextScaleOffset](VoxelAnimationClipAsset& clip)
        {
            VoxelAnimationPartTrack& track = ensure_part_track(clip, partId);
            VoxelAnimationTransformKeyframe& key = ensure_transform_key(track, time, VoxelAnimationTransformKeyframe{
                .timeSeconds = time,
                .localPosition = nextPositionOffset,
                .localRotation = nextRotationOffset,
                .localScale = nextScaleOffset
            });
            key.localPosition = nextPositionOffset;
            key.localRotation = nextRotationOffset;
            key.localScale = nextScaleOffset;
        });
    }
}

void AnimationEditorScene::draw_clip_timeline_window(const std::shared_ptr<const VoxelAssemblyAsset>& selectedAssembly)
{
    if (_mode != EditorMode::Clip)
    {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + 460.0f, viewport->WorkPos.y + viewport->WorkSize.y - 264.0f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2(std::max(420.0f, viewport->WorkSize.x - 476.0f), 248.0f),
            ImGuiCond_FirstUseEver);
    }

    if (!ImGui::Begin("Animation Timeline"))
    {
        ImGui::End();
        return;
    }

    if (selectedAssembly == nullptr)
    {
        ImGui::TextWrapped("Select an assembly to begin clip authoring.");
        ImGui::End();
        return;
    }

    const float frameRate = std::max(_clip.frameRateHint, 1.0f);
    const int maxFrame = std::max(frame_from_time(_clip.durationSeconds, frameRate), 1);
    std::vector<ClipSequencerEntry> sequencerEntries = build_clip_sequencer_entries(_clip);

    ImGui::Text("Clip: %s", _clip.assetId.empty() ? "<unsaved>" : _clip.assetId.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Frame %d / %d", frame_from_time(_previewTimeSeconds, frameRate), maxFrame);
    ImGui::SameLine();
    if (ImGui::Button(_playing ? "Pause" : "Play"))
    {
        _playing = !_playing;
    }
    ImGui::SameLine();
    if (ImGui::Button("Jump To Start"))
    {
        _playing = false;
        _previewTimeSeconds = 0.0f;
        _previewDirty = true;
    }

    if (sequencerEntries.empty())
    {
        _sequencerSelectedEntry = -1;
        _sequencerSelectedKeyIndex = -1;
        ImGui::Separator();
        ImGui::TextWrapped("No lanes yet. Key a transform, visibility, or binding state from the inspector and it will appear here.");
        ImGui::End();
        return;
    }

    const int previousSelectedEntry = _sequencerSelectedEntry;
    int selectedEntry = std::clamp(_sequencerSelectedEntry, -1, static_cast<int>(sequencerEntries.size()) - 1);
    if (!_selectedPartId.empty())
    {
        const bool currentSelectionMatchesPart =
            selectedEntry >= 0 &&
            selectedEntry < static_cast<int>(sequencerEntries.size()) &&
            sequencerEntries[static_cast<size_t>(selectedEntry)].partId == _selectedPartId;
        const auto matchingEntry = std::ranges::find_if(sequencerEntries, [&](const ClipSequencerEntry& entry)
        {
            return entry.partId == _selectedPartId;
        });
        if (!currentSelectionMatchesPart && matchingEntry != sequencerEntries.end())
        {
            selectedEntry = static_cast<int>(std::distance(sequencerEntries.begin(), matchingEntry));
            _sequencerSelectedKeyIndex = -1;
        }
    }

    ClipSequencerModel sequencer(std::move(sequencerEntries), 0, maxFrame, &selectedEntry);
    int currentFrame = std::clamp(frame_from_time(_previewTimeSeconds, frameRate), 0, maxFrame);
    const int initialFrame = currentFrame;
    selectedEntry = std::clamp(selectedEntry, -1, sequencer.GetItemCount() - 1);
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
        _playing = false;
        const float nextPreviewTime = std::clamp(time_from_frame(currentFrame, frameRate), 0.0f, _clip.durationSeconds);
        if (!keyframe_time_matches(_previewTimeSeconds, nextPreviewTime))
        {
            _previewTimeSeconds = nextPreviewTime;
            _previewDirty = true;
        }
    }

    if (selectedEntry < 0 || selectedEntry >= sequencer.GetItemCount())
    {
        _sequencerSelectedKeyIndex = -1;
        ImGui::End();
        return;
    }

    const ClipSequencerEntry& entry = sequencer.entries()[static_cast<size_t>(selectedEntry)];
    if (selectedEntry != previousSelectedEntry && !entry.partId.empty() && _selectedPartId != entry.partId)
    {
        _selectedPartId = entry.partId;
        _selectionOverlayDirty = true;
    }

    const int keyCount = lane_key_count(_clip, entry);
    if (selectedEntry != previousSelectedEntry)
    {
        _sequencerSelectedKeyIndex = keyCount > 0 ? 0 : -1;
    }
    _sequencerSelectedKeyIndex = std::clamp(_sequencerSelectedKeyIndex, keyCount > 0 ? 0 : -1, keyCount - 1);
    if (_sequencerSelectedKeyIndex >= 0)
    {
        _sequencerSelectedKeyFrame = lane_key_frame(_clip, entry, _sequencerSelectedKeyIndex);
    }

    ImGui::SeparatorText("Lane");
    ImGui::TextWrapped("%s", entry.label.c_str());
    if (!entry.partId.empty())
    {
        ImGui::Text("Part: %s", entry.partId.c_str());
    }
    if (ImGui::Button("Delete Lane"))
    {
        try_delete_selection(DeleteSelection{ .kind = DeleteSelectionKind::ClipTimelineLane });
        ImGui::End();
        return;
    }

    if (keyCount <= 0)
    {
        ImGui::TextWrapped("This lane currently has no keys to edit.");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Keys");
    for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
    {
        ImGui::PushID(keyIndex);
        const int keyFrame = lane_key_frame(_clip, entry, keyIndex);
        const std::string buttonLabel = std::format("{}f", keyFrame);
        if (ImGui::Selectable(buttonLabel.c_str(), _sequencerSelectedKeyIndex == keyIndex, 0, ImVec2(56.0f, 0.0f)))
        {
            _sequencerSelectedKeyIndex = keyIndex;
            _sequencerSelectedKeyFrame = keyFrame;
            _previewTimeSeconds = std::clamp(time_from_frame(keyFrame, frameRate), 0.0f, _clip.durationSeconds);
            _previewDirty = true;
        }
        if (((keyIndex + 1) % 10) != 0 && keyIndex + 1 < keyCount)
        {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }

    if (_sequencerSelectedKeyIndex >= 0)
    {
        int selectedKeyFrame = _sequencerSelectedKeyFrame;
        if (ImGui::SliderInt("Selected Key Frame", &selectedKeyFrame, 0, maxFrame))
        {
            _sequencerSelectedKeyFrame = selectedKeyFrame;
        }
        if (ImGui::IsItemActivated())
        {
            _playing = false;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            const int keyIndex = _sequencerSelectedKeyIndex;
            const int targetFrame = _sequencerSelectedKeyFrame;
            const ClipSequencerEntry laneEntry = entry;
            apply_clip_edit("Move timeline key", [laneEntry, keyIndex, frameRate, targetFrame](VoxelAnimationClipAsset& clip)
            {
                move_lane_key(clip, laneEntry, keyIndex, time_from_frame(targetFrame, frameRate));
            });
            _previewTimeSeconds = std::clamp(time_from_frame(targetFrame, frameRate), 0.0f, _clip.durationSeconds);
            _previewDirty = true;
        }

        if (ImGui::Button("Jump To Key"))
        {
            _playing = false;
            _previewTimeSeconds = std::clamp(time_from_frame(_sequencerSelectedKeyFrame, frameRate), 0.0f, _clip.durationSeconds);
            _previewDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Playhead Time"))
        {
            const int keyIndex = _sequencerSelectedKeyIndex;
            const int targetFrame = std::clamp(frame_from_time(_previewTimeSeconds, frameRate), 0, maxFrame);
            const ClipSequencerEntry laneEntry = entry;
            apply_clip_edit("Move key to playhead", [laneEntry, keyIndex, frameRate, targetFrame](VoxelAnimationClipAsset& clip)
            {
                move_lane_key(clip, laneEntry, keyIndex, time_from_frame(targetFrame, frameRate));
            });
            _sequencerSelectedKeyFrame = targetFrame;
        }
        ImGui::SameLine();
        if (ImGui::Button("-1f"))
        {
            const int keyIndex = _sequencerSelectedKeyIndex;
            const int targetFrame = std::max(_sequencerSelectedKeyFrame - 1, 0);
            const ClipSequencerEntry laneEntry = entry;
            apply_clip_edit("Nudge key earlier", [laneEntry, keyIndex, frameRate, targetFrame](VoxelAnimationClipAsset& clip)
            {
                move_lane_key(clip, laneEntry, keyIndex, time_from_frame(targetFrame, frameRate));
            });
            _sequencerSelectedKeyFrame = targetFrame;
            _previewTimeSeconds = std::clamp(time_from_frame(targetFrame, frameRate), 0.0f, _clip.durationSeconds);
            _previewDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+1f"))
        {
            const int keyIndex = _sequencerSelectedKeyIndex;
            const int targetFrame = std::min(_sequencerSelectedKeyFrame + 1, maxFrame);
            const ClipSequencerEntry laneEntry = entry;
            apply_clip_edit("Nudge key later", [laneEntry, keyIndex, frameRate, targetFrame](VoxelAnimationClipAsset& clip)
            {
                move_lane_key(clip, laneEntry, keyIndex, time_from_frame(targetFrame, frameRate));
            });
            _sequencerSelectedKeyFrame = targetFrame;
            _previewTimeSeconds = std::clamp(time_from_frame(targetFrame, frameRate), 0.0f, _clip.durationSeconds);
            _previewDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Key"))
        {
            try_delete_selection(DeleteSelection{ .kind = DeleteSelectionKind::ClipTimelineKey });
        }
    }

    ImGui::End();
}

void AnimationEditorScene::draw_controller_graph_window()
{
    if (_mode != EditorMode::Controller)
    {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + 462.0f, viewport->WorkPos.y + 16.0f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2(std::max(520.0f, viewport->WorkSize.x - 478.0f), std::max(420.0f, viewport->WorkSize.y - 32.0f)),
            ImGuiCond_FirstUseEver);
    }

    ImGui::Begin("Controller Graph");
    if (_controller.layers.empty())
    {
        ImGui::TextWrapped("Add a layer in the controller inspector to start building a state graph.");
        ImGui::End();
        return;
    }

    if (_selectedControllerLayerId.empty() || _controller.find_layer(_selectedControllerLayerId) == nullptr)
    {
        _selectedControllerLayerId = _controller.layers.front().layerId;
    }

    if (_controllerNodeEditor != nullptr)
    {
        ImNodes::EditorContextSet(_controllerNodeEditor);
    }

    const VoxelAnimationLayerDefinition* const selectedLayer = _controller.find_layer(_selectedControllerLayerId);
    if (selectedLayer == nullptr)
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginCombo("Graph Layer", _selectedControllerLayerId.c_str()))
    {
        for (const VoxelAnimationLayerDefinition& layer : _controller.layers)
        {
            const bool selected = layer.layerId == _selectedControllerLayerId;
            if (ImGui::Selectable(layer.layerId.c_str(), selected))
            {
                _selectedControllerLayerId = layer.layerId;
                _selectedControllerStateId.clear();
                _selectedControllerTransitionKey.clear();
            }
        }
        ImGui::EndCombo();
    }

    const std::string selectedLayerId = _selectedControllerLayerId;
    if (ImGui::Button("Add State"))
    {
        const std::string newStateId = make_unique_identifier("state", "state", [&](const std::string_view candidate)
        {
            return selectedLayer->find_state(candidate) != nullptr;
        });
        apply_controller_edit("Add controller state", [selectedLayerId, newStateId](VoxelAnimationControllerAsset& controller)
        {
            VoxelAnimationLayerDefinition* const layer = controller.find_layer(selectedLayerId);
            if (layer == nullptr)
            {
                return;
            }

            layer->states.push_back(VoxelAnimationStateDefinition{
                .stateId = newStateId,
                .displayName = newStateId
            });
            if (layer->entryStateId.empty())
            {
                layer->entryStateId = newStateId;
            }
        });
        _selectedControllerStateId = newStateId;
        _selectedControllerTransitionKey.clear();
    }

    const bool hasSelectedState = !_selectedControllerStateId.empty() && selectedLayer->find_state(_selectedControllerStateId) != nullptr;
    if (hasSelectedState)
    {
        ImGui::SameLine();
        if (ImGui::Button("Delete State"))
        {
            try_delete_selection(DeleteSelection{ .kind = DeleteSelectionKind::ControllerState });
        }
    }

    if (!_selectedControllerTransitionKey.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button("Delete Transition"))
        {
            try_delete_selection(DeleteSelection{ .kind = DeleteSelectionKind::ControllerTransition });
        }
    }

    ImGui::Separator();

    std::unordered_map<int, std::string> nodeToState{};
    std::unordered_map<int, std::string> inputPinToState{};
    std::unordered_map<int, std::string> outputPinToState{};
    std::unordered_map<int, std::string> linkToSelectionKey{};
    nodeToState.reserve(selectedLayer->states.size());
    inputPinToState.reserve(selectedLayer->states.size());
    outputPinToState.reserve(selectedLayer->states.size());
    linkToSelectionKey.reserve(selectedLayer->transitions.size());

    ImNodes::BeginNodeEditor();
    for (size_t stateIndex = 0; stateIndex < selectedLayer->states.size(); ++stateIndex)
    {
        const VoxelAnimationStateDefinition& state = selectedLayer->states[stateIndex];
        const int nodeId = stable_imnodes_id("state", selectedLayer->layerId, state.stateId);
        const int inputPinId = stable_imnodes_id("state_input", selectedLayer->layerId, state.stateId);
        const int outputPinId = stable_imnodes_id("state_output", selectedLayer->layerId, state.stateId);
        nodeToState.insert_or_assign(nodeId, state.stateId);
        inputPinToState.insert_or_assign(inputPinId, state.stateId);
        outputPinToState.insert_or_assign(outputPinId, state.stateId);

        if (!_initializedControllerNodeIds.contains(nodeId))
        {
            const float x = 48.0f + static_cast<float>(stateIndex % 4) * 260.0f;
            const float y = 72.0f + static_cast<float>(stateIndex / 4) * 180.0f;
            ImNodes::SetNodeEditorSpacePos(nodeId, ImVec2(x, y));
            _initializedControllerNodeIds.insert(nodeId);
        }

        if (selectedLayer->entryStateId == state.stateId)
        {
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(64, 118, 76, 255));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(90, 154, 108, 255));
        }

        ImNodes::BeginNode(nodeId);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(state.displayName.empty() ? state.stateId.c_str() : state.displayName.c_str());
        ImNodes::EndNodeTitleBar();

        ImNodes::BeginInputAttribute(inputPinId);
        ImGui::TextUnformatted("In");
        ImNodes::EndInputAttribute();

        ImGui::TextDisabled("%s", state.nodeType == VoxelAnimationStateNodeType::ClipPlayer ? "Clip Player" : "Blend Space 2D");
        if (state.nodeType == VoxelAnimationStateNodeType::ClipPlayer)
        {
            ImGui::TextWrapped("%s", state.clipAssetId.empty() ? "<no clip>" : state.clipAssetId.c_str());
        }
        else
        {
            ImGui::TextWrapped("%s", state.blendSpaceId.empty() ? "<no blend space>" : state.blendSpaceId.c_str());
        }
        ImGui::Text("Speed %.2f", state.playbackSpeed);
        ImGui::Text("Root %s", state.rootMotionMode == RootMotionMode::Ignore
            ? "Ignore"
            : (state.rootMotionMode == RootMotionMode::ExtractPlanar ? "Planar" : "Full"));
        if (selectedLayer->entryStateId == state.stateId)
        {
            ImGui::TextColored(ImVec4(0.64f, 0.88f, 0.68f, 1.0f), "Entry");
        }

        ImNodes::BeginOutputAttribute(outputPinId);
        ImGui::Indent(80.0f);
        ImGui::TextUnformatted("Out");
        ImNodes::EndOutputAttribute();

        ImNodes::EndNode();
        if (selectedLayer->entryStateId == state.stateId)
        {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
    }

    for (const VoxelAnimationTransitionDefinition& transition : selectedLayer->transitions)
    {
        const int sourcePinId = stable_imnodes_id("state_output", selectedLayer->layerId, transition.sourceStateId);
        const int targetPinId = stable_imnodes_id("state_input", selectedLayer->layerId, transition.targetStateId);
        const int linkId = stable_imnodes_id("transition", selectedLayer->layerId, transition.sourceStateId, transition.targetStateId);
        linkToSelectionKey.insert_or_assign(
            linkId,
            transition_selection_key(selectedLayer->layerId, transition.sourceStateId, transition.targetStateId));
        ImNodes::Link(linkId, sourcePinId, targetPinId);
    }
    ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_TopRight);
    ImNodes::EndNodeEditor();

    int startedAttributeId = 0;
    int endedAttributeId = 0;
    if (ImNodes::IsLinkCreated(&startedAttributeId, &endedAttributeId))
    {
        const auto sourceIt = outputPinToState.find(startedAttributeId);
        const auto targetIt = inputPinToState.find(endedAttributeId);
        if (sourceIt != outputPinToState.end() &&
            targetIt != inputPinToState.end() &&
            sourceIt->second != targetIt->second &&
            find_transition(*selectedLayer, sourceIt->second, targetIt->second) == nullptr)
        {
            const std::string sourceStateId = sourceIt->second;
            const std::string targetStateId = targetIt->second;
            apply_controller_edit("Add controller transition", [selectedLayerId, sourceStateId, targetStateId](VoxelAnimationControllerAsset& controller)
            {
                VoxelAnimationLayerDefinition* const layer = controller.find_layer(selectedLayerId);
                if (layer == nullptr)
                {
                    return;
                }

                layer->transitions.push_back(VoxelAnimationTransitionDefinition{
                    .sourceStateId = sourceStateId,
                    .targetStateId = targetStateId
                });
            });
            _selectedControllerTransitionKey = transition_selection_key(selectedLayerId, sourceStateId, targetStateId);
            _selectedControllerStateId.clear();
        }
    }

    int destroyedLinkId = 0;
    if (ImNodes::IsLinkDestroyed(&destroyedLinkId))
    {
        if (const auto it = linkToSelectionKey.find(destroyedLinkId); it != linkToSelectionKey.end())
        {
            _selectedControllerTransitionKey = it->second;
            _selectedControllerStateId.clear();
            try_delete_selection(DeleteSelection{ .kind = DeleteSelectionKind::ControllerTransition });
        }
    }

    const int selectedNodeCount = ImNodes::NumSelectedNodes();
    if (selectedNodeCount > 0)
    {
        std::vector<int> selectedNodes(static_cast<size_t>(selectedNodeCount));
        ImNodes::GetSelectedNodes(selectedNodes.data());
        if (const auto it = nodeToState.find(selectedNodes.front()); it != nodeToState.end())
        {
            _selectedControllerStateId = it->second;
            _selectedControllerTransitionKey.clear();
        }
    }
    else
    {
        const int selectedLinkCount = ImNodes::NumSelectedLinks();
        if (selectedLinkCount > 0)
        {
            std::vector<int> selectedLinks(static_cast<size_t>(selectedLinkCount));
            ImNodes::GetSelectedLinks(selectedLinks.data());
            if (const auto it = linkToSelectionKey.find(selectedLinks.front()); it != linkToSelectionKey.end())
            {
                _selectedControllerTransitionKey = it->second;
                _selectedControllerStateId.clear();
            }
        }
    }

    ImGui::End();
}

void AnimationEditorScene::draw_editor_window()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr)
    {
        const float timelineHeight = _mode == EditorMode::Clip ? 280.0f : 0.0f;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + 16.0f, viewport->WorkPos.y + 16.0f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2(430.0f, std::max(420.0f, viewport->WorkSize.y - timelineHeight - 32.0f)),
            ImGuiCond_FirstUseEver);
    }

    ImGui::Begin("Animation Editor");

    const std::shared_ptr<const VoxelAssemblyAsset> selectedAssembly =
        !_selectedAssemblyId.empty() ? _assemblyAssetManager.load_or_get(_selectedAssemblyId) : nullptr;

    int mode = _mode == EditorMode::Clip ? 0 : 1;
    if (ImGui::RadioButton("Clip Mode", mode == 0))
    {
        _mode = EditorMode::Clip;
        _playing = false;
        _previewDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Controller Mode", mode == 1))
    {
        _mode = EditorMode::Controller;
        reset_controller_preview_state(true);
        sync_controller_preview_parameters();
        _playing = false;
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
                reset_controller_preview_state(true);
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
        if (_mode == EditorMode::Controller)
        {
            reset_controller_preview_state(true);
            sync_controller_preview_parameters();
        }
        else
        {
            _previewTimeSeconds = 0.0f;
        }
        _previewDirty = true;
    }
    if (_mode == EditorMode::Clip)
    {
        if (ImGui::SliderFloat("Time", &_previewTimeSeconds, 0.0f, std::max(_clip.durationSeconds, 0.1f)))
        {
            _previewDirty = true;
        }
        ImGui::Text("Selected Part: %s", _selectedPartId.empty() ? "<none>" : _selectedPartId.c_str());
    }
    else
    {
        ImGui::Text("Preview Time: %.2fs", _previewTimeSeconds);
        ImGui::Text("Selected Layer: %s", _selectedControllerLayerId.empty() ? "<none>" : _selectedControllerLayerId.c_str());
        ImGui::Text("Selected State: %s", _selectedControllerStateId.empty() ? "<none>" : _selectedControllerStateId.c_str());
    }
    if (ImGui::Checkbox("Auto Center Preview", &_autoCenterPreview))
    {
        _previewDirty = true;
    }
    if (_mode == EditorMode::Clip)
    {
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
        if (ImGui::Button("Delete Clip"))
        {
            delete_selected_clip_asset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Undo Clip"))
        {
            (void)_clipSession.undo([this]()
            {
                _previewDirty = true;
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Redo Clip"))
        {
            (void)_clipSession.redo([this]()
            {
                _previewDirty = true;
            });
        }
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
        if (ImGui::Button("Delete Controller"))
        {
            delete_selected_controller_asset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Undo Controller"))
        {
            (void)_controllerSession.undo([this]()
            {
                reset_controller_preview_state(true);
                _previewDirty = true;
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Redo Controller"))
        {
            (void)_controllerSession.redo([this]()
            {
                reset_controller_preview_state(true);
                _previewDirty = true;
            });
        }
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
            ImGui::SeparatorText("Timeline");
            ImGui::TextWrapped("Clip timing now lives in the separate Animation Timeline window below so the inspector can stay focused on part edits.");

            ImGui::SeparatorText("Assembly Parts");
            if (ImGui::BeginChild("ClipPartList", ImVec2(0.0f, 140.0f), true))
            {
                for (const VoxelAssemblyPartDefinition& assemblyPart : selectedAssembly->parts)
                {
                    const bool selected = _selectedPartId == assemblyPart.partId;
                    if (ImGui::Selectable(assemblyPart.partId.c_str(), selected))
                    {
                        _selectedPartId = assemblyPart.partId;
                        _sequencerSelectedEntry = -1;
                        _sequencerSelectedKeyIndex = -1;
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
                    compose_binding_relative_position(effectiveBindingState, sampledPart);
                const glm::quat effectiveRotation =
                    compose_binding_relative_rotation(effectiveBindingState, sampledPart);
                const glm::vec3 effectiveScale =
                    compose_binding_relative_scale(effectiveBindingState, sampledPart);
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
                    const glm::vec3 nextPosition = relative_position_from_binding(effectiveBindingState, effectivePosition);
                    const glm::quat nextRotation = relative_rotation_from_binding(effectiveBindingState, effectiveRotation);
                    const glm::vec3 nextScale = relative_scale_from_binding(effectiveBindingState, effectiveScale);
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
                    const glm::vec3 nextPosition = relative_position_from_binding(effectiveBindingState, localPosition);
                    const glm::quat nextRotation = relative_rotation_from_binding(effectiveBindingState, effectiveRotation);
                    const glm::vec3 nextScale = relative_scale_from_binding(effectiveBindingState, effectiveScale);
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
                    const glm::vec3 nextPosition = relative_position_from_binding(effectiveBindingState, effectivePosition);
                    const glm::vec3 nextScale = relative_scale_from_binding(effectiveBindingState, effectiveScale);
                    const glm::quat nextRotation = relative_rotation_from_binding(effectiveBindingState, glm::quat(glm::radians(nextDegrees)));
                    apply_clip_edit("Key local rotation", [partId, time, nextRotation, nextPosition, nextScale](VoxelAnimationClipAsset& clip)
                    {
                        VoxelAnimationPartTrack& track = ensure_part_track(clip, partId);
                        VoxelAnimationTransformKeyframe& key = ensure_transform_key(track, time, VoxelAnimationTransformKeyframe{
                            .timeSeconds = time,
                            .localPosition = nextPosition,
                            .localRotation = nextRotation,
                            .localScale = nextScale
                        });
                        key.localRotation = nextRotation;
                    });
                }

                glm::vec3 localScale = effectiveScale;
                if (ImGui::InputFloat3("Local Scale", &localScale.x, "%.3f"))
                {
                    const std::string partId = selectedPart->partId;
                    const float time = _previewTimeSeconds;
                    const glm::vec3 nextScale = relative_scale_from_binding(effectiveBindingState, glm::max(localScale, glm::vec3(0.001f)));
                    const glm::vec3 nextPosition = relative_position_from_binding(effectiveBindingState, effectivePosition);
                    const glm::quat nextRotation = relative_rotation_from_binding(effectiveBindingState, effectiveRotation);
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
                            VoxelAnimationPartTrack& track = ensure_part_track(clip, partId);
                            VoxelAnimationTransformKeyframe& poseKey = ensure_transform_key(track, time, VoxelAnimationTransformKeyframe{
                                .timeSeconds = time,
                                .localPosition = glm::vec3(0.0f),
                                .localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                .localScale = glm::vec3(1.0f)
                            });
                            poseKey.localPosition = glm::vec3(0.0f);
                            poseKey.localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                            poseKey.localScale = glm::vec3(1.0f);
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
                                VoxelAnimationPartTrack& partTrack = ensure_part_track(clip, partId);
                                VoxelAnimationTransformKeyframe& poseKey = ensure_transform_key(partTrack, time, VoxelAnimationTransformKeyframe{
                                    .timeSeconds = time,
                                    .localPosition = glm::vec3(0.0f),
                                    .localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                    .localScale = glm::vec3(1.0f)
                                });
                                poseKey.localPosition = glm::vec3(0.0f);
                                poseKey.localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                                poseKey.localScale = glm::vec3(1.0f);
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
        ImGui::SeparatorText("Controller");
        sync_controller_preview_parameters();

        char controllerAssetIdBuffer[128]{};
        copy_cstr_truncating(controllerAssetIdBuffer, _controller.assetId);
        if (ImGui::InputText("Controller Asset Id", controllerAssetIdBuffer, IM_ARRAYSIZE(controllerAssetIdBuffer)))
        {
            const std::string nextAssetId = controllerAssetIdBuffer;
            apply_controller_edit("Rename controller asset id", [nextAssetId](VoxelAnimationControllerAsset& controller)
            {
                controller.assetId = nextAssetId;
            });
        }

        char controllerDisplayNameBuffer[128]{};
        copy_cstr_truncating(controllerDisplayNameBuffer, _controller.displayName);
        if (ImGui::InputText("Controller Display Name", controllerDisplayNameBuffer, IM_ARRAYSIZE(controllerDisplayNameBuffer)))
        {
            const std::string nextDisplayName = controllerDisplayNameBuffer;
            apply_controller_edit("Rename controller display name", [nextDisplayName](VoxelAnimationControllerAsset& controller)
            {
                controller.displayName = nextDisplayName;
            });
        }

        if (ImGui::Button("Reset Preview State"))
        {
            reset_controller_preview_state(true);
            sync_controller_preview_parameters();
            _previewDirty = true;
        }

        ImGui::SeparatorText("Preview Parameters");
        ImGui::TextWrapped("Parameters only affect the controller when a transition condition or blend-space axis references them. A parameter by itself does not change states.");
        if (_controller.parameters.empty())
        {
            ImGui::TextWrapped("Add parameters to drive the graph preview and runtime conditions.");
        }

        if (!_selectedControllerLayerId.empty())
        {
            const auto runtimeIt = std::ranges::find_if(_previewAnimation.layerStates, [&](const VoxelAnimationLayerPlaybackState& state)
            {
                return state.layerId == _selectedControllerLayerId;
            });
            if (runtimeIt != _previewAnimation.layerStates.end())
            {
                ImGui::Text("Preview Active State: %s", runtimeIt->activeStateId.empty() ? "<none>" : runtimeIt->activeStateId.c_str());
                if (runtimeIt->inTransition)
                {
                    ImGui::Text("Preview Transition: %s -> %s", runtimeIt->activeStateId.c_str(), runtimeIt->targetStateId.c_str());
                }
            }
        }
        else
        {
            for (size_t parameterIndex = 0; parameterIndex < _controller.parameters.size(); ++parameterIndex)
            {
                const VoxelAnimationParameterDefinition& parameter = _controller.parameters[parameterIndex];
                ImGui::PushID(static_cast<int>(parameterIndex));
                if (parameter.type == VoxelAnimationParameterType::Float)
                {
                    float previewValue = _previewAnimation.floatParameters.contains(parameter.parameterId)
                        ? _previewAnimation.floatParameters.at(parameter.parameterId)
                        : parameter.defaultFloatValue;
                    const std::string label = std::format("{}##PreviewFloat", parameter.displayName.empty() ? parameter.parameterId : parameter.displayName);
                    if (ImGui::DragFloat(label.c_str(), &previewValue, 0.05f))
                    {
                        set_voxel_animation_float_parameter(_previewAnimation, parameter.parameterId, previewValue);
                        _previewDirty = true;
                    }
                }
                else if (parameter.type == VoxelAnimationParameterType::Bool)
                {
                    bool previewValue = _previewAnimation.boolParameters.contains(parameter.parameterId)
                        ? _previewAnimation.boolParameters.at(parameter.parameterId)
                        : parameter.defaultBoolValue;
                    const std::string label = std::format("{}##PreviewBool", parameter.displayName.empty() ? parameter.parameterId : parameter.displayName);
                    if (ImGui::Checkbox(label.c_str(), &previewValue))
                    {
                        set_voxel_animation_bool_parameter(_previewAnimation, parameter.parameterId, previewValue);
                        _previewDirty = true;
                    }
                }
                else
                {
                    const std::string label = std::format("Fire {}##PreviewTrigger", parameter.displayName.empty() ? parameter.parameterId : parameter.displayName);
                    if (ImGui::Button(label.c_str()))
                    {
                        trigger_voxel_animation_parameter(_previewAnimation, parameter.parameterId);
                        _previewDirty = true;
                    }
                }
                ImGui::PopID();
            }
        }

        ImGui::SeparatorText("Parameters");
        if (ImGui::Button("Add Float"))
        {
            const std::string parameterId = make_unique_identifier("float_param", "float_param", [&](const std::string_view candidate)
            {
                return _controller.find_parameter(candidate) != nullptr;
            });
            apply_controller_edit("Add float parameter", [parameterId](VoxelAnimationControllerAsset& controller)
            {
                controller.parameters.push_back(VoxelAnimationParameterDefinition{
                    .parameterId = parameterId,
                    .displayName = parameterId,
                    .type = VoxelAnimationParameterType::Float
                });
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Bool"))
        {
            const std::string parameterId = make_unique_identifier("bool_param", "bool_param", [&](const std::string_view candidate)
            {
                return _controller.find_parameter(candidate) != nullptr;
            });
            apply_controller_edit("Add bool parameter", [parameterId](VoxelAnimationControllerAsset& controller)
            {
                controller.parameters.push_back(VoxelAnimationParameterDefinition{
                    .parameterId = parameterId,
                    .displayName = parameterId,
                    .type = VoxelAnimationParameterType::Bool
                });
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Trigger"))
        {
            const std::string parameterId = make_unique_identifier("trigger_param", "trigger_param", [&](const std::string_view candidate)
            {
                return _controller.find_parameter(candidate) != nullptr;
            });
            apply_controller_edit("Add trigger parameter", [parameterId](VoxelAnimationControllerAsset& controller)
            {
                controller.parameters.push_back(VoxelAnimationParameterDefinition{
                    .parameterId = parameterId,
                    .displayName = parameterId,
                    .type = VoxelAnimationParameterType::Trigger
                });
            });
        }

        if (ImGui::BeginChild("ControllerParameters", ImVec2(0.0f, 130.0f), true))
        {
            for (size_t parameterIndex = 0; parameterIndex < _controller.parameters.size(); ++parameterIndex)
            {
                const VoxelAnimationParameterDefinition& parameter = _controller.parameters[parameterIndex];
                ImGui::PushID(static_cast<int>(parameterIndex));
                ImGui::Text("%s (%s)", parameter.parameterId.c_str(), parameter_type_label(parameter.type));
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    apply_controller_edit("Delete parameter", [parameterIndex](VoxelAnimationControllerAsset& controller)
                    {
                        if (parameterIndex < controller.parameters.size())
                        {
                            controller.parameters.erase(controller.parameters.begin() + static_cast<std::ptrdiff_t>(parameterIndex));
                        }
                    });
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::SeparatorText("Layers");
        if (ImGui::Button("Add Layer"))
        {
            const std::string layerId = make_unique_identifier("layer", "layer", [&](const std::string_view candidate)
            {
                return _controller.find_layer(candidate) != nullptr;
            });
            apply_controller_edit("Add controller layer", [layerId](VoxelAnimationControllerAsset& controller)
            {
                controller.layers.push_back(VoxelAnimationLayerDefinition{
                    .layerId = layerId,
                    .displayName = layerId
                });
            });
            _selectedControllerLayerId = layerId;
            _selectedControllerStateId.clear();
            _selectedControllerTransitionKey.clear();
        }

        if (ImGui::BeginChild("ControllerLayers", ImVec2(0.0f, 100.0f), true))
        {
            for (const VoxelAnimationLayerDefinition& layer : _controller.layers)
            {
                const bool selected = layer.layerId == _selectedControllerLayerId;
                if (ImGui::Selectable(layer.layerId.c_str(), selected))
                {
                    _selectedControllerLayerId = layer.layerId;
                    _selectedControllerStateId.clear();
                    _selectedControllerTransitionKey.clear();
                }
            }
        }
        ImGui::EndChild();

        VoxelAnimationLayerDefinition* const selectedLayer = !_selectedControllerLayerId.empty()
            ? _controller.find_layer(_selectedControllerLayerId)
            : nullptr;
        if (selectedLayer != nullptr)
        {
            int blendMode = selectedLayer->blendMode == VoxelAnimationLayerBlendMode::Override ? 0 : 1;
            if (ImGui::Combo("Layer Blend", &blendMode, ControllerBlendModeLabels))
            {
                const std::string layerId = selectedLayer->layerId;
                apply_controller_edit("Edit layer blend mode", [layerId, blendMode](VoxelAnimationControllerAsset& controller)
                {
                    if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                    {
                        layer->blendMode = blendMode == 0
                            ? VoxelAnimationLayerBlendMode::Override
                            : VoxelAnimationLayerBlendMode::Additive;
                    }
                });
            }

            float weight = selectedLayer->weight;
            if (ImGui::SliderFloat("Layer Weight", &weight, 0.0f, 1.0f))
            {
                const std::string layerId = selectedLayer->layerId;
                apply_controller_edit("Edit layer weight", [layerId, weight](VoxelAnimationControllerAsset& controller)
                {
                    if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                    {
                        layer->weight = weight;
                    }
                });
            }

            if (ImGui::BeginCombo("Entry State", selectedLayer->entryStateId.empty() ? "<none>" : selectedLayer->entryStateId.c_str()))
            {
                for (const VoxelAnimationStateDefinition& state : selectedLayer->states)
                {
                    const bool selected = state.stateId == selectedLayer->entryStateId;
                    if (ImGui::Selectable(state.stateId.c_str(), selected))
                    {
                        const std::string layerId = selectedLayer->layerId;
                        const std::string stateId = state.stateId;
                        apply_controller_edit("Edit layer entry state", [layerId, stateId](VoxelAnimationControllerAsset& controller)
                        {
                            if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                            {
                                layer->entryStateId = stateId;
                            }
                        });
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Delete Layer"))
            {
                try_delete_selection(DeleteSelection{ .kind = DeleteSelectionKind::ControllerLayer });
            }
        }

        ImGui::SeparatorText("Selected Graph Item");
        if (selectedLayer != nullptr && !_selectedControllerStateId.empty())
        {
            const VoxelAnimationStateDefinition* const selectedState = selectedLayer->find_state(_selectedControllerStateId);
            if (selectedState != nullptr)
            {
                ImGui::Text("State: %s", selectedState->stateId.c_str());
                ImGui::TextDisabled("%s", selectedState->displayName.c_str());
                int nodeType = static_cast<int>(selectedState->nodeType);
                if (ImGui::Combo("Node Type", &nodeType, ControllerNodeTypeLabels))
                {
                    const std::string layerId = selectedLayer->layerId;
                    const std::string stateId = selectedState->stateId;
                    apply_controller_edit("Edit state node type", [layerId, stateId, nodeType](VoxelAnimationControllerAsset& controller)
                    {
                        if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                        {
                            for (VoxelAnimationStateDefinition& state : layer->states)
                            {
                                if (state.stateId == stateId)
                                {
                                    state.nodeType = static_cast<VoxelAnimationStateNodeType>(nodeType);
                                }
                            }
                        }
                    });
                }

                if (selectedState->nodeType == VoxelAnimationStateNodeType::ClipPlayer)
                {
                    if (ImGui::BeginCombo("Clip", selectedState->clipAssetId.empty() ? "<none>" : selectedState->clipAssetId.c_str()))
                    {
                        for (const std::string& clipAssetId : _clipAssetIds)
                        {
                            const bool selected = clipAssetId == selectedState->clipAssetId;
                            if (ImGui::Selectable(clipAssetId.c_str(), selected))
                            {
                                const std::string layerId = selectedLayer->layerId;
                                const std::string stateId = selectedState->stateId;
                                apply_controller_edit("Edit state clip", [layerId, stateId, clipAssetId](VoxelAnimationControllerAsset& controller)
                                {
                                    if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                                    {
                                        for (VoxelAnimationStateDefinition& state : layer->states)
                                        {
                                            if (state.stateId == stateId)
                                            {
                                                state.clipAssetId = clipAssetId;
                                            }
                                        }
                                    }
                                });
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::TextWrapped("Blend-space states are supported by runtime; richer blend-space authoring is the next editor pass.");
                }

                float playbackSpeed = selectedState->playbackSpeed;
                if (ImGui::InputFloat("Playback Speed", &playbackSpeed, 0.0f, 0.0f, "%.3f"))
                {
                    const std::string layerId = selectedLayer->layerId;
                    const std::string stateId = selectedState->stateId;
                    apply_controller_edit("Edit state playback speed", [layerId, stateId, playbackSpeed](VoxelAnimationControllerAsset& controller)
                    {
                        if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                        {
                            for (VoxelAnimationStateDefinition& state : layer->states)
                            {
                                if (state.stateId == stateId)
                                {
                                    state.playbackSpeed = playbackSpeed;
                                }
                            }
                        }
                    });
                }

                int rootMotionMode = static_cast<int>(selectedState->rootMotionMode);
                if (ImGui::Combo("Root Motion", &rootMotionMode, ControllerRootMotionLabels))
                {
                    const std::string layerId = selectedLayer->layerId;
                    const std::string stateId = selectedState->stateId;
                    apply_controller_edit("Edit state root motion", [layerId, stateId, rootMotionMode](VoxelAnimationControllerAsset& controller)
                    {
                        if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                        {
                            for (VoxelAnimationStateDefinition& state : layer->states)
                            {
                                if (state.stateId == stateId)
                                {
                                    state.rootMotionMode = static_cast<RootMotionMode>(rootMotionMode);
                                }
                            }
                        }
                    });
                }
            }
        }
        else if (selectedLayer != nullptr && !_selectedControllerTransitionKey.empty())
        {
            const auto [sourceStateId, targetStateId] = transition_endpoints_from_key(_selectedControllerTransitionKey);
            const VoxelAnimationTransitionDefinition* const transition = find_transition(*selectedLayer, sourceStateId, targetStateId);
            if (transition != nullptr)
            {
                ImGui::Text("%s -> %s", sourceStateId.c_str(), targetStateId.c_str());
                float durationSeconds = transition->durationSeconds;
                if (ImGui::InputFloat("Duration", &durationSeconds, 0.0f, 0.0f, "%.3f"))
                {
                    const std::string layerId = selectedLayer->layerId;
                    apply_controller_edit("Edit transition duration", [layerId, sourceStateId, targetStateId, durationSeconds](VoxelAnimationControllerAsset& controller)
                    {
                        if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                        {
                            for (VoxelAnimationTransitionDefinition& transition : layer->transitions)
                            {
                                if (transition.sourceStateId == sourceStateId && transition.targetStateId == targetStateId)
                                {
                                    transition.durationSeconds = std::max(durationSeconds, 0.0f);
                                }
                            }
                        }
                    });
                }

                bool requiresExitTime = transition->requiresExitTime;
                if (ImGui::Checkbox("Requires Exit Time", &requiresExitTime))
                {
                    const std::string layerId = selectedLayer->layerId;
                    apply_controller_edit("Edit transition exit time flag", [layerId, sourceStateId, targetStateId, requiresExitTime](VoxelAnimationControllerAsset& controller)
                    {
                        if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                        {
                            for (VoxelAnimationTransitionDefinition& transition : layer->transitions)
                            {
                                if (transition.sourceStateId == sourceStateId && transition.targetStateId == targetStateId)
                                {
                                    transition.requiresExitTime = requiresExitTime;
                                }
                            }
                        }
                    });
                }

                float exitTimeNormalized = transition->exitTimeNormalized;
                if (ImGui::SliderFloat("Exit Time", &exitTimeNormalized, 0.0f, 1.0f))
                {
                    const std::string layerId = selectedLayer->layerId;
                    apply_controller_edit("Edit transition exit time", [layerId, sourceStateId, targetStateId, exitTimeNormalized](VoxelAnimationControllerAsset& controller)
                    {
                        if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                        {
                            for (VoxelAnimationTransitionDefinition& transition : layer->transitions)
                            {
                                if (transition.sourceStateId == sourceStateId && transition.targetStateId == targetStateId)
                                {
                                    transition.exitTimeNormalized = std::clamp(exitTimeNormalized, 0.0f, 1.0f);
                                }
                            }
                        }
                    });
                }

                ImGui::SeparatorText("Conditions");
                ImGui::TextWrapped("All conditions on this transition must match for the controller to leave '%s' and enter '%s'.", sourceStateId.c_str(), targetStateId.c_str());
                if (ImGui::Button("Add Condition"))
                {
                    const std::string layerId = selectedLayer->layerId;
                    apply_controller_edit("Add transition condition", [layerId, sourceStateId, targetStateId](VoxelAnimationControllerAsset& controller)
                    {
                        if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                        {
                            for (VoxelAnimationTransitionDefinition& transition : layer->transitions)
                            {
                                if (transition.sourceStateId == sourceStateId && transition.targetStateId == targetStateId)
                                {
                                    transition.conditions.push_back(VoxelAnimationCondition{});
                                    break;
                                }
                            }
                        }
                    });
                }

                for (size_t conditionIndex = 0; conditionIndex < transition->conditions.size(); ++conditionIndex)
                {
                    const VoxelAnimationCondition& condition = transition->conditions[conditionIndex];
                    ImGui::PushID(static_cast<int>(conditionIndex));
                    if (ImGui::BeginCombo("Parameter", condition.parameterId.empty() ? "<none>" : condition.parameterId.c_str()))
                    {
                        for (const VoxelAnimationParameterDefinition& parameter : _controller.parameters)
                        {
                            const bool selected = parameter.parameterId == condition.parameterId;
                            if (ImGui::Selectable(parameter.parameterId.c_str(), selected))
                            {
                                const std::string layerId = selectedLayer->layerId;
                                const std::string parameterId = parameter.parameterId;
                                apply_controller_edit("Edit transition condition parameter", [layerId, sourceStateId, targetStateId, conditionIndex, parameterId](VoxelAnimationControllerAsset& controller)
                                {
                                    if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                                    {
                                        for (VoxelAnimationTransitionDefinition& transition : layer->transitions)
                                        {
                                            if (transition.sourceStateId == sourceStateId &&
                                                transition.targetStateId == targetStateId &&
                                                conditionIndex < transition.conditions.size())
                                            {
                                                transition.conditions[conditionIndex].parameterId = parameterId;
                                            }
                                        }
                                    }
                                });
                            }
                        }
                        ImGui::EndCombo();
                    }

                    int opIndex = static_cast<int>(condition.op);
                    if (ImGui::Combo("Operation", &opIndex, ControllerConditionOpLabels))
                    {
                        const std::string layerId = selectedLayer->layerId;
                        apply_controller_edit("Edit transition condition operation", [layerId, sourceStateId, targetStateId, conditionIndex, opIndex](VoxelAnimationControllerAsset& controller)
                        {
                            if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                            {
                                for (VoxelAnimationTransitionDefinition& transition : layer->transitions)
                                {
                                    if (transition.sourceStateId == sourceStateId &&
                                        transition.targetStateId == targetStateId &&
                                        conditionIndex < transition.conditions.size())
                                    {
                                        transition.conditions[conditionIndex].op = static_cast<VoxelAnimationConditionOp>(opIndex);
                                    }
                                }
                            }
                        });
                    }

                    if (condition.op != VoxelAnimationConditionOp::IsTrue &&
                        condition.op != VoxelAnimationConditionOp::IsFalse &&
                        condition.op != VoxelAnimationConditionOp::Triggered)
                    {
                        float compareValue = condition.value;
                        if (ImGui::InputFloat("Compare Value", &compareValue, 0.0f, 0.0f, "%.3f"))
                        {
                            const std::string layerId = selectedLayer->layerId;
                            apply_controller_edit("Edit transition condition value", [layerId, sourceStateId, targetStateId, conditionIndex, compareValue](VoxelAnimationControllerAsset& controller)
                            {
                                if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                                {
                                    for (VoxelAnimationTransitionDefinition& transition : layer->transitions)
                                    {
                                        if (transition.sourceStateId == sourceStateId &&
                                            transition.targetStateId == targetStateId &&
                                            conditionIndex < transition.conditions.size())
                                        {
                                            transition.conditions[conditionIndex].value = compareValue;
                                        }
                                    }
                                }
                            });
                        }
                    }

                    if (ImGui::SmallButton("Delete Condition"))
                    {
                        const std::string layerId = selectedLayer->layerId;
                        apply_controller_edit("Delete transition condition", [layerId, sourceStateId, targetStateId, conditionIndex](VoxelAnimationControllerAsset& controller)
                        {
                            if (VoxelAnimationLayerDefinition* const layer = controller.find_layer(layerId); layer != nullptr)
                            {
                                for (VoxelAnimationTransitionDefinition& transition : layer->transitions)
                                {
                                    if (transition.sourceStateId == sourceStateId &&
                                        transition.targetStateId == targetStateId &&
                                        conditionIndex < transition.conditions.size())
                                    {
                                        transition.conditions.erase(transition.conditions.begin() + static_cast<std::ptrdiff_t>(conditionIndex));
                                        break;
                                    }
                                }
                            }
                        });
                        ImGui::PopID();
                        break;
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }
        else
        {
            ImGui::TextWrapped("Select a state or transition in the Controller Graph window to edit it here.");
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", _statusMessage.c_str());
    ImGui::End();

    draw_clip_timeline_window(selectedAssembly);
    draw_controller_graph_window();
}
