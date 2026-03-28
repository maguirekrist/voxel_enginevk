#include "voxel_animation_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

#include <glm/common.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>

namespace
{
    constexpr float ConditionEpsilon = 0.0001f;

    struct SampledTransform
    {
        bool valid{false};
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    struct ClipContribution
    {
        std::shared_ptr<const VoxelAnimationClipAsset> clip{};
        float weight{0.0f};
    };

    struct NodeSample
    {
        VoxelAssemblyPose pose{};
        std::vector<ClipContribution> clips{};
        SampledTransform rootTransform{};
        RootMotionMode rootMotionMode{RootMotionMode::Ignore};
    };

    [[nodiscard]] glm::quat normalized_quat(glm::quat value)
    {
        if (glm::length(value) <= 0.00001f)
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        return glm::normalize(value);
    }

    [[nodiscard]] float sample_time(const VoxelAnimationClipAsset& clip, const float timeSeconds)
    {
        const float duration = std::max(clip.durationSeconds, 0.001f);
        if (clip.loopMode == VoxelAnimationLoopMode::Loop)
        {
            const float t = std::fmod(timeSeconds, duration);
            return t < 0.0f ? t + duration : t;
        }

        return std::clamp(timeSeconds, 0.0f, duration);
    }

    [[nodiscard]] int loop_count(const VoxelAnimationClipAsset& clip, const float timeSeconds)
    {
        if (clip.loopMode != VoxelAnimationLoopMode::Loop)
        {
            return 0;
        }
        return static_cast<int>(std::floor(std::max(0.0f, timeSeconds) / std::max(clip.durationSeconds, 0.001f)));
    }

    template <typename TKeyframe>
    [[nodiscard]] const TKeyframe* previous_key(const std::vector<TKeyframe>& keys, const float timeSeconds)
    {
        if (keys.empty())
        {
            return nullptr;
        }

        for (size_t index = keys.size(); index > 0; --index)
        {
            if (keys[index - 1].timeSeconds <= timeSeconds)
            {
                return &keys[index - 1];
            }
        }

        return &keys.front();
    }

    [[nodiscard]] SampledTransform sample_transform_keys(
        const VoxelAnimationPartTrack& track,
        const VoxelAnimationClipAsset& clip,
        const float timeSeconds,
        const bool unwrapped)
    {
        SampledTransform result{};
        if (track.transformKeys.empty())
        {
            return result;
        }

        const float localTime = sample_time(clip, timeSeconds);
        if (track.transformKeys.size() == 1 || localTime <= track.transformKeys.front().timeSeconds)
        {
            const auto& key = track.transformKeys.front();
            result.valid = true;
            result.position = key.localPosition;
            result.rotation = normalized_quat(key.localRotation);
            result.scale = key.localScale;
        }
        else if (localTime >= track.transformKeys.back().timeSeconds)
        {
            const auto& key = track.transformKeys.back();
            result.valid = true;
            result.position = key.localPosition;
            result.rotation = normalized_quat(key.localRotation);
            result.scale = key.localScale;
        }
        else
        {
            for (size_t index = 0; index + 1 < track.transformKeys.size(); ++index)
            {
                const auto& lhs = track.transformKeys[index];
                const auto& rhs = track.transformKeys[index + 1];
                if (localTime < lhs.timeSeconds || localTime > rhs.timeSeconds)
                {
                    continue;
                }

                const float segment = std::max(rhs.timeSeconds - lhs.timeSeconds, 0.0001f);
                const float alpha = std::clamp((localTime - lhs.timeSeconds) / segment, 0.0f, 1.0f);
                result.valid = true;
                result.position = glm::mix(lhs.localPosition, rhs.localPosition, alpha);
                result.rotation = glm::slerp(normalized_quat(lhs.localRotation), normalized_quat(rhs.localRotation), alpha);
                result.scale = glm::mix(lhs.localScale, rhs.localScale, alpha);
                break;
            }
        }

        if (!result.valid || !unwrapped || clip.loopMode != VoxelAnimationLoopMode::Loop || track.transformKeys.empty())
        {
            return result;
        }

        const int cycles = loop_count(clip, timeSeconds);
        if (cycles <= 0)
        {
            return result;
        }

        const auto& startKey = track.transformKeys.front();
        const auto& endKey = track.transformKeys.back();
        result.position += (endKey.localPosition - startKey.localPosition) * static_cast<float>(cycles);

        glm::quat cycleRotation = normalized_quat(endKey.localRotation) * glm::inverse(normalized_quat(startKey.localRotation));
        glm::quat accumulatedCycle{1.0f, 0.0f, 0.0f, 0.0f};
        for (int cycleIndex = 0; cycleIndex < cycles; ++cycleIndex)
        {
            accumulatedCycle = normalized_quat(cycleRotation * accumulatedCycle);
        }
        result.rotation = normalized_quat(accumulatedCycle * result.rotation);
        return result;
    }

    [[nodiscard]] std::optional<bool> sample_visibility(
        const VoxelAnimationPartTrack& track,
        const VoxelAnimationClipAsset& clip,
        const float timeSeconds)
    {
        const auto* key = previous_key(track.visibilityKeys, sample_time(clip, timeSeconds));
        return key != nullptr ? std::optional<bool>{key->visible} : std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> sample_binding_state(
        const VoxelAnimationBindingTrack& track,
        const VoxelAnimationClipAsset& clip,
        const float timeSeconds)
    {
        const auto* key = previous_key(track.keys, sample_time(clip, timeSeconds));
        if (key == nullptr || key->stateId.empty())
        {
            return std::nullopt;
        }
        return key->stateId;
    }

    [[nodiscard]] std::optional<float> float_parameter(
        const VoxelAnimationComponent& component,
        const VoxelAnimationControllerAsset& controller,
        const std::string_view parameterId)
    {
        if (auto it = component.floatParameters.find(std::string(parameterId)); it != component.floatParameters.end())
        {
            return it->second;
        }

        const auto* definition = controller.find_parameter(parameterId);
        return definition != nullptr ? std::optional<float>{definition->defaultFloatValue} : std::nullopt;
    }

    [[nodiscard]] std::optional<bool> bool_parameter(
        const VoxelAnimationComponent& component,
        const VoxelAnimationControllerAsset& controller,
        const std::string_view parameterId)
    {
        if (auto it = component.boolParameters.find(std::string(parameterId)); it != component.boolParameters.end())
        {
            return it->second;
        }

        const auto* definition = controller.find_parameter(parameterId);
        return definition != nullptr ? std::optional<bool>{definition->defaultBoolValue} : std::nullopt;
    }

    [[nodiscard]] bool trigger_parameter(
        const VoxelAnimationComponent& component,
        const std::string_view parameterId)
    {
        if (auto it = component.triggerParameters.find(std::string(parameterId)); it != component.triggerParameters.end())
        {
            return it->second;
        }
        return false;
    }

    [[nodiscard]] bool transition_condition_matches(
        const VoxelAnimationCondition& condition,
        const VoxelAnimationComponent& component,
        const VoxelAnimationControllerAsset& controller)
    {
        const float value = float_parameter(component, controller, condition.parameterId).value_or(0.0f);
        const bool flag = bool_parameter(component, controller, condition.parameterId).value_or(false);
        const bool trigger = trigger_parameter(component, condition.parameterId);

        switch (condition.op)
        {
        case VoxelAnimationConditionOp::Greater:
            return value > condition.value;
        case VoxelAnimationConditionOp::Less:
            return value < condition.value;
        case VoxelAnimationConditionOp::LessEqual:
            return value <= condition.value;
        case VoxelAnimationConditionOp::Equal:
            return std::abs(value - condition.value) <= ConditionEpsilon;
        case VoxelAnimationConditionOp::NotEqual:
            return std::abs(value - condition.value) > ConditionEpsilon;
        case VoxelAnimationConditionOp::IsTrue:
            return flag;
        case VoxelAnimationConditionOp::IsFalse:
            return !flag;
        case VoxelAnimationConditionOp::Triggered:
            return trigger;
        case VoxelAnimationConditionOp::GreaterEqual:
        default:
            return value >= condition.value;
        }
    }

    [[nodiscard]] std::vector<ClipContribution> gather_blend_space_samples(
        const VoxelAnimationBlendSpace2D& blendSpace,
        VoxelAnimationClipAssetManager& clipAssetManager,
        const float x,
        const float y)
    {
        std::vector<ClipContribution> result{};
        if (blendSpace.samples.empty())
        {
            return result;
        }

        std::vector<float> xs{};
        std::vector<float> ys{};
        xs.reserve(blendSpace.samples.size());
        ys.reserve(blendSpace.samples.size());
        for (const auto& sample : blendSpace.samples)
        {
            xs.push_back(sample.x);
            ys.push_back(sample.y);
        }
        std::ranges::sort(xs);
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        std::ranges::sort(ys);
        ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

        const auto upperX = std::ranges::upper_bound(xs, x);
        const auto upperY = std::ranges::upper_bound(ys, y);
        const float x1 = upperX == xs.begin() ? xs.front() : *(upperX - 1);
        const float x2 = upperX == xs.end() ? xs.back() : *upperX;
        const float y1 = upperY == ys.begin() ? ys.front() : *(upperY - 1);
        const float y2 = upperY == ys.end() ? ys.back() : *upperY;
        const float tx = x1 == x2 ? 0.0f : std::clamp((x - x1) / std::max(x2 - x1, 0.0001f), 0.0f, 1.0f);
        const float ty = y1 == y2 ? 0.0f : std::clamp((y - y1) / std::max(y2 - y1, 0.0001f), 0.0f, 1.0f);

        const std::array<std::pair<glm::vec2, float>, 4> targets{
            std::pair{glm::vec2(x1, y1), (1.0f - tx) * (1.0f - ty)},
            std::pair{glm::vec2(x2, y1), tx * (1.0f - ty)},
            std::pair{glm::vec2(x1, y2), (1.0f - tx) * ty},
            std::pair{glm::vec2(x2, y2), tx * ty}
        };

        for (const auto& [coord, weight] : targets)
        {
            if (weight <= 0.0f)
            {
                continue;
            }

            const auto it = std::ranges::find_if(blendSpace.samples, [&](const VoxelAnimationBlendSpaceSample& sample)
            {
                return std::abs(sample.x - coord.x) <= ConditionEpsilon &&
                    std::abs(sample.y - coord.y) <= ConditionEpsilon;
            });
            if (it == blendSpace.samples.end())
            {
                continue;
            }

            const auto clip = clipAssetManager.load_or_get(it->clipAssetId);
            if (clip != nullptr)
            {
                result.push_back(ClipContribution{ .clip = clip, .weight = weight });
            }
        }

        const float totalWeight = std::accumulate(result.begin(), result.end(), 0.0f, [](const float total, const ClipContribution& contribution)
        {
            return total + contribution.weight;
        });
        if (totalWeight > 0.0001f)
        {
            for (auto& contribution : result)
            {
                contribution.weight /= totalWeight;
            }
        }

        return result;
    }

    [[nodiscard]] glm::quat weighted_quaternion(const std::vector<std::pair<glm::quat, float>>& inputs)
    {
        if (inputs.empty())
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        glm::vec4 accumulator(0.0f);
        const glm::quat anchor = normalized_quat(inputs.front().first);
        for (const auto& [rotationValue, weight] : inputs)
        {
            glm::quat rotation = normalized_quat(rotationValue);
            if (glm::dot(anchor, rotation) < 0.0f)
            {
                rotation = -rotation;
            }
            accumulator += glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w) * weight;
        }

        return normalized_quat(glm::quat(accumulator.w, accumulator.x, accumulator.y, accumulator.z));
    }

    [[nodiscard]] SampledTransform base_transform_for_part(
        const VoxelAssemblyAsset& assemblyAsset,
        const VoxelAssemblyComponent& assemblyComponent,
        const VoxelAssemblyPose& pose,
        const std::string_view partId)
    {
        std::string bindingStateId{};
        if (const auto* posePart = pose.find_part(partId);
            posePart != nullptr && posePart->bindingStateId.has_value())
        {
            bindingStateId = posePart->bindingStateId.value();
        }
        if (bindingStateId.empty())
        {
            if (auto it = assemblyComponent.partBindingStateOverrides.find(std::string(partId));
                it != assemblyComponent.partBindingStateOverrides.end())
            {
                bindingStateId = it->second;
            }
        }

        const VoxelAssemblyBindingState* state = nullptr;
        if (!bindingStateId.empty())
        {
            state = assemblyAsset.find_binding_state(partId, bindingStateId);
        }
        if (state == nullptr)
        {
            state = assemblyAsset.default_binding_state(partId);
        }

        SampledTransform result{};
        if (state != nullptr)
        {
            result.valid = true;
            result.position = state->localPositionOffset;
            result.rotation = state->localRotationOffset;
            result.scale = state->localScale;
        }
        return result;
    }

    [[nodiscard]] std::unordered_set<std::string> layer_mask(
        const VoxelAssemblyAsset& assemblyAsset,
        const VoxelAnimationLayerDefinition& layer)
    {
        std::unordered_set<std::string> mask{};
        if (layer.maskedPartIds.empty())
        {
            for (const auto& part : assemblyAsset.parts)
            {
                mask.insert(part.partId);
            }
            return mask;
        }

        for (const auto& partId : layer.maskedPartIds)
        {
            mask.insert(partId);
        }

        if (!layer.includeMaskedPartDescendants)
        {
            return mask;
        }

        bool added = true;
        while (added)
        {
            added = false;
            for (const auto& part : assemblyAsset.parts)
            {
                const auto* state = assemblyAsset.default_binding_state(part.partId);
                if (state != nullptr && mask.contains(state->parentPartId) && !mask.contains(part.partId))
                {
                    mask.insert(part.partId);
                    added = true;
                }
            }
        }

        return mask;
    }

    void fire_clip_events(
        VoxelAnimationComponent& component,
        const VoxelAnimationClipAsset& clip,
        const float previousTime,
        const float currentTime)
    {
        const float duration = std::max(clip.durationSeconds, 0.001f);
        const float previousLocal = sample_time(clip, previousTime);
        const float currentLocal = sample_time(clip, currentTime);
        const bool wrapped = clip.loopMode == VoxelAnimationLoopMode::Loop &&
            (currentLocal < previousLocal || (currentTime - previousTime) >= duration);

        for (const auto& track : clip.eventTracks)
        {
            for (const auto& key : track.events)
            {
                const bool crossed = wrapped
                    ? (key.timeSeconds > previousLocal || key.timeSeconds <= currentLocal)
                    : (key.timeSeconds > previousLocal && key.timeSeconds <= currentLocal);
                if (!crossed)
                {
                    continue;
                }

                component.pendingEvents.push_back(VoxelAnimationQueuedEvent{
                    .clipAssetId = clip.assetId,
                    .trackId = track.trackId,
                    .eventId = key.eventId,
                    .payload = key.payload,
                    .timeSeconds = key.timeSeconds
                });
            }
        }
    }

    [[nodiscard]] NodeSample sample_clip_node(
        const std::shared_ptr<const VoxelAnimationClipAsset>& clip,
        const float timeSeconds)
    {
        NodeSample sample{};
        if (clip == nullptr)
        {
            return sample;
        }

        for (const auto& track : clip->partTracks)
        {
            VoxelAssemblyPosePart part{ .partId = track.partId };
            const SampledTransform transform = sample_transform_keys(track, *clip, timeSeconds, false);
            if (transform.valid)
            {
                part.localPosition = transform.position;
                part.localRotation = transform.rotation;
                part.localScale = transform.scale;
            }
            part.visible = sample_visibility(track, *clip, timeSeconds);
            if (part.localPosition.has_value() || part.localRotation.has_value() || part.localScale.has_value() || part.visible.has_value())
            {
                sample.pose.parts.push_back(std::move(part));
            }
        }

        for (const auto& track : clip->bindingTracks)
        {
            if (const auto stateId = sample_binding_state(track, *clip, timeSeconds); stateId.has_value())
            {
                sample.pose.ensure_part(track.partId).bindingStateId = stateId;
            }
        }

        const std::string sourcePartId = clip->motionSourcePartId;
        if (!sourcePartId.empty())
        {
            if (const auto* track = clip->find_part_track(sourcePartId); track != nullptr)
            {
                sample.rootTransform = sample_transform_keys(*track, *clip, timeSeconds, true);
            }
        }

        sample.clips.push_back(ClipContribution{ .clip = clip, .weight = 1.0f });
        sample.rootMotionMode = RootMotionMode::Ignore;
        return sample;
    }

    [[nodiscard]] NodeSample sample_blend_space_node(
        const VoxelAnimationBlendSpace2D& blendSpace,
        const VoxelAnimationControllerAsset& controller,
        VoxelAnimationClipAssetManager& clipAssetManager,
        const VoxelAnimationComponent& component,
        const float timeSeconds)
    {
        NodeSample sample{};
        const float x = float_parameter(component, controller, blendSpace.xParameterId).value_or(0.0f);
        const float y = float_parameter(component, controller, blendSpace.yParameterId).value_or(0.0f);
        sample.clips = gather_blend_space_samples(blendSpace, clipAssetManager, x, y);

        std::unordered_set<std::string> partIds{};
        for (const auto& contribution : sample.clips)
        {
            for (const auto& track : contribution.clip->partTracks)
            {
                partIds.insert(track.partId);
            }
            for (const auto& track : contribution.clip->bindingTracks)
            {
                partIds.insert(track.partId);
            }
        }

        for (const auto& partId : partIds)
        {
            glm::vec3 accumulatedPosition(0.0f);
            glm::vec3 accumulatedScale(0.0f);
            float linearWeight = 0.0f;
            std::vector<std::pair<glm::quat, float>> rotations{};
            float visibleTrue = 0.0f;
            float visibleWeight = 0.0f;
            std::unordered_map<std::string, float> bindingVotes{};

            for (const auto& contribution : sample.clips)
            {
                if (const auto* track = contribution.clip->find_part_track(partId); track != nullptr)
                {
                    const SampledTransform transform = sample_transform_keys(*track, *contribution.clip, timeSeconds, false);
                    if (transform.valid)
                    {
                        accumulatedPosition += transform.position * contribution.weight;
                        accumulatedScale += transform.scale * contribution.weight;
                        linearWeight += contribution.weight;
                        rotations.emplace_back(transform.rotation, contribution.weight);
                    }
                    if (const auto visible = sample_visibility(*track, *contribution.clip, timeSeconds); visible.has_value())
                    {
                        visibleWeight += contribution.weight;
                        if (visible.value())
                        {
                            visibleTrue += contribution.weight;
                        }
                    }
                }

                if (const auto* track = contribution.clip->find_binding_track(partId); track != nullptr)
                {
                    if (const auto stateId = sample_binding_state(*track, *contribution.clip, timeSeconds); stateId.has_value())
                    {
                        bindingVotes[stateId.value()] += contribution.weight;
                    }
                }
            }

            VoxelAssemblyPosePart part{ .partId = partId };
            if (linearWeight > 0.0f)
            {
                part.localPosition = accumulatedPosition / linearWeight;
                part.localScale = accumulatedScale / linearWeight;
            }
            if (!rotations.empty())
            {
                part.localRotation = weighted_quaternion(rotations);
            }
            if (visibleWeight > 0.0f)
            {
                part.visible = visibleTrue >= (visibleWeight * 0.5f);
            }
            if (!bindingVotes.empty())
            {
                const auto best = std::ranges::max_element(bindingVotes, [](const auto& lhs, const auto& rhs)
                {
                    return lhs.second < rhs.second;
                });
                part.bindingStateId = best->first;
            }

            if (part.localPosition.has_value() || part.localRotation.has_value() || part.localScale.has_value() ||
                part.visible.has_value() || part.bindingStateId.has_value())
            {
                sample.pose.parts.push_back(std::move(part));
            }
        }

        return sample;
    }

    [[nodiscard]] NodeSample sample_state_node(
        const VoxelAnimationStateDefinition& state,
        const VoxelAnimationControllerAsset& controller,
        VoxelAnimationClipAssetManager& clipAssetManager,
        const VoxelAnimationComponent& component,
        const float timeSeconds)
    {
        if (state.nodeType == VoxelAnimationStateNodeType::BlendSpace2D)
        {
            if (const auto* blendSpace = controller.find_blend_space(state.blendSpaceId); blendSpace != nullptr)
            {
                NodeSample sample = sample_blend_space_node(*blendSpace, controller, clipAssetManager, component, timeSeconds);
                sample.rootMotionMode = state.rootMotionMode;
                return sample;
            }
            return {};
        }

        NodeSample sample = sample_clip_node(clipAssetManager.load_or_get(state.clipAssetId), timeSeconds);
        sample.rootMotionMode = state.rootMotionMode;
        return sample;
    }

    [[nodiscard]] bool transition_ready(
        const VoxelAnimationTransitionDefinition& transition,
        const VoxelAnimationLayerPlaybackState& layerState,
        const VoxelAnimationLayerDefinition& layer,
        const VoxelAnimationControllerAsset& controller,
        VoxelAnimationClipAssetManager& clipAssetManager,
        const VoxelAnimationComponent& component)
    {
        const auto* state = layer.find_state(layerState.activeStateId);
        if (state == nullptr)
        {
            return false;
        }

        if (transition.requiresExitTime && state->nodeType == VoxelAnimationStateNodeType::ClipPlayer)
        {
            const auto clip = clipAssetManager.load_or_get(state->clipAssetId);
            if (clip == nullptr)
            {
                return false;
            }

            const float normalized = std::clamp(layerState.stateTimeSeconds / std::max(clip->durationSeconds, 0.001f), 0.0f, 1.0f);
            if (normalized < transition.exitTimeNormalized)
            {
                return false;
            }
        }

        return std::ranges::all_of(transition.conditions, [&](const VoxelAnimationCondition& condition)
        {
            return transition_condition_matches(condition, component, controller);
        });
    }

    void apply_override_pose(VoxelAssemblyPose& destination, const VoxelAssemblyPose& source, const std::unordered_set<std::string>& mask)
    {
        for (const auto& part : source.parts)
        {
            if (!mask.contains(part.partId))
            {
                continue;
            }

            auto& target = destination.ensure_part(part.partId);
            if (part.bindingStateId.has_value()) target.bindingStateId = part.bindingStateId;
            if (part.localPosition.has_value()) target.localPosition = part.localPosition;
            if (part.localRotation.has_value()) target.localRotation = part.localRotation;
            if (part.localScale.has_value()) target.localScale = part.localScale;
            if (part.visible.has_value()) target.visible = part.visible;
        }
    }

    void apply_additive_pose(
        VoxelAssemblyPose& destination,
        const VoxelAssemblyPose& source,
        const VoxelAssemblyAsset& assemblyAsset,
        const VoxelAssemblyComponent& assemblyComponent,
        const std::unordered_set<std::string>& mask,
        const float weight)
    {
        for (const auto& part : source.parts)
        {
            if (!mask.contains(part.partId))
            {
                continue;
            }

            const SampledTransform base = base_transform_for_part(assemblyAsset, assemblyComponent, VoxelAssemblyPose{}, part.partId);
            const SampledTransform current = base_transform_for_part(assemblyAsset, assemblyComponent, destination, part.partId);
            auto& target = destination.ensure_part(part.partId);

            if (part.bindingStateId.has_value())
            {
                target.bindingStateId = part.bindingStateId;
            }
            if (part.localPosition.has_value() && base.valid)
            {
                const glm::vec3 currentPosition = target.localPosition.value_or(current.position);
                target.localPosition = currentPosition + ((part.localPosition.value() - base.position) * weight);
            }
            if (part.localRotation.has_value() && base.valid)
            {
                const glm::quat currentRotation = target.localRotation.value_or(current.rotation);
                const glm::quat delta = normalized_quat(part.localRotation.value() * glm::inverse(base.rotation));
                target.localRotation = normalized_quat(currentRotation * glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), delta, weight));
            }
            if (part.localScale.has_value() && base.valid)
            {
                const glm::vec3 currentScale = target.localScale.value_or(current.scale);
                glm::vec3 delta(1.0f);
                delta.x = base.scale.x != 0.0f ? part.localScale->x / base.scale.x : 1.0f;
                delta.y = base.scale.y != 0.0f ? part.localScale->y / base.scale.y : 1.0f;
                delta.z = base.scale.z != 0.0f ? part.localScale->z / base.scale.z : 1.0f;
                target.localScale = currentScale * glm::mix(glm::vec3(1.0f), delta, weight);
            }
            if (part.visible.has_value())
            {
                target.visible = part.visible;
            }
        }
    }

    [[nodiscard]] float yaw_from_quat(const glm::quat& value)
    {
        const glm::quat normalized = normalized_quat(value);
        return std::atan2(
            2.0f * ((normalized.w * normalized.y) + (normalized.x * normalized.z)),
            1.0f - (2.0f * ((normalized.y * normalized.y) + (normalized.z * normalized.z))));
    }

    [[nodiscard]] glm::quat yaw_quat(const float radians)
    {
        return glm::angleAxis(radians, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void apply_root_motion(
        VoxelAnimationComponent& component,
        VoxelAnimationLayerPlaybackState& layerState,
        const std::string& sourcePartId,
        const RootMotionMode mode,
        const SampledTransform& currentRoot)
    {
        component.rootMotion = {};
        component.rootMotion.mode = mode;
        component.rootMotion.sourcePartId = sourcePartId;

        if (mode == RootMotionMode::Ignore || !currentRoot.valid)
        {
            layerState.hasRootMotionReference = false;
            return;
        }

        if (!layerState.hasRootMotionReference)
        {
            layerState.hasRootMotionReference = true;
            layerState.rootMotionReferenceTranslation = currentRoot.position;
            layerState.rootMotionReferenceRotation = currentRoot.rotation;
            layerState.previousExtractedTranslation = glm::vec3(0.0f);
            layerState.previousExtractedRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            return;
        }

        glm::vec3 accumulatedTranslation = currentRoot.position - layerState.rootMotionReferenceTranslation;
        glm::quat accumulatedRotation = normalized_quat(currentRoot.rotation * glm::inverse(layerState.rootMotionReferenceRotation));
        if (mode == RootMotionMode::ExtractPlanar)
        {
            accumulatedTranslation.y = 0.0f;
            accumulatedRotation = yaw_quat(yaw_from_quat(accumulatedRotation));
        }

        component.rootMotion.translationDeltaLocal = accumulatedTranslation - layerState.previousExtractedTranslation;
        component.rootMotion.rotationDeltaLocal = normalized_quat(accumulatedRotation * glm::inverse(layerState.previousExtractedRotation));
        component.rootMotion.valid = true;
        layerState.previousExtractedTranslation = accumulatedTranslation;
        layerState.previousExtractedRotation = accumulatedRotation;

        if (auto* rootPart = component.currentPose.find_part(sourcePartId); rootPart != nullptr)
        {
            if (rootPart->localPosition.has_value())
            {
                glm::vec3 rebased = rootPart->localPosition.value() - accumulatedTranslation;
                if (mode == RootMotionMode::ExtractPlanar)
                {
                    rebased.y = rootPart->localPosition->y;
                }
                rootPart->localPosition = rebased;
            }
            if (rootPart->localRotation.has_value())
            {
                rootPart->localRotation = normalized_quat(glm::inverse(accumulatedRotation) * rootPart->localRotation.value());
            }
        }
    }
}

void set_voxel_animation_float_parameter(VoxelAnimationComponent& component, const std::string_view parameterId, const float value)
{
    component.floatParameters.insert_or_assign(std::string(parameterId), value);
}

void set_voxel_animation_bool_parameter(VoxelAnimationComponent& component, const std::string_view parameterId, const bool value)
{
    component.boolParameters.insert_or_assign(std::string(parameterId), value);
}

void trigger_voxel_animation_parameter(VoxelAnimationComponent& component, const std::string_view parameterId)
{
    component.triggerParameters.insert_or_assign(std::string(parameterId), true);
}

void clear_voxel_animation_events(VoxelAnimationComponent& component)
{
    component.pendingEvents.clear();
}

void consume_voxel_animation_root_motion(VoxelAnimationComponent& component)
{
    component.rootMotion = {};
}

VoxelAssemblyPose sample_voxel_animation_clip_pose(const VoxelAnimationClipAsset& clip, const float timeSeconds)
{
    return sample_clip_node(std::make_shared<VoxelAnimationClipAsset>(clip), timeSeconds).pose;
}

glm::vec3 sample_voxel_animation_clip_motion_source_position(
    const VoxelAnimationClipAsset& clip,
    const std::string_view fallbackSourcePartId,
    const float timeSeconds)
{
    const std::string sourcePartId = !clip.motionSourcePartId.empty() ? clip.motionSourcePartId : std::string(fallbackSourcePartId);
    if (sourcePartId.empty())
    {
        return glm::vec3(0.0f);
    }

    if (const auto* track = clip.find_part_track(sourcePartId); track != nullptr)
    {
        const SampledTransform transform = sample_transform_keys(*track, clip, timeSeconds, true);
        return transform.position;
    }

    return glm::vec3(0.0f);
}

void tick_voxel_animation_component(
    VoxelAnimationComponent& component,
    const VoxelAssemblyComponent& assemblyComponent,
    const VoxelAssemblyAsset& assemblyAsset,
    VoxelAnimationControllerAssetManager& controllerAssetManager,
    VoxelAnimationClipAssetManager& clipAssetManager,
    const float deltaTime)
{
    component.pendingEvents.clear();
    component.currentPose.clear();
    component.rootMotion = {};

    if (!component.enabled || component.controllerAssetId.empty())
    {
        return;
    }

    const auto controller = controllerAssetManager.load_or_get(component.controllerAssetId);
    if (controller == nullptr || controller->assemblyAssetId != assemblyAsset.assetId)
    {
        return;
    }

    for (const auto& definition : controller->parameters)
    {
        if (definition.type == VoxelAnimationParameterType::Float && !component.floatParameters.contains(definition.parameterId))
            component.floatParameters.insert_or_assign(definition.parameterId, definition.defaultFloatValue);
        else if (definition.type == VoxelAnimationParameterType::Bool && !component.boolParameters.contains(definition.parameterId))
            component.boolParameters.insert_or_assign(definition.parameterId, definition.defaultBoolValue);
        else if (definition.type == VoxelAnimationParameterType::Trigger && !component.triggerParameters.contains(definition.parameterId))
            component.triggerParameters.insert_or_assign(definition.parameterId, false);
    }

    for (const auto& layer : controller->layers)
    {
        if (std::ranges::find_if(component.layerStates, [&](const VoxelAnimationLayerPlaybackState& state) { return state.layerId == layer.layerId; }) == component.layerStates.end())
        {
            component.layerStates.push_back(VoxelAnimationLayerPlaybackState{
                .layerId = layer.layerId,
                .activeStateId = !layer.entryStateId.empty() ? layer.entryStateId : (!layer.states.empty() ? layer.states.front().stateId : std::string{})
            });
        }
    }

    for (size_t layerIndex = 0; layerIndex < controller->layers.size(); ++layerIndex)
    {
        const auto& layer = controller->layers[layerIndex];
        auto runtimeIt = std::ranges::find_if(component.layerStates, [&](const VoxelAnimationLayerPlaybackState& state) { return state.layerId == layer.layerId; });
        if (runtimeIt == component.layerStates.end() || runtimeIt->activeStateId.empty())
        {
            continue;
        }
        VoxelAnimationLayerPlaybackState& runtime = *runtimeIt;

        if (!runtime.inTransition)
        {
            for (const auto& transition : layer.transitions)
            {
                if (transition.sourceStateId == runtime.activeStateId &&
                    transition_ready(transition, runtime, layer, *controller, clipAssetManager, component))
                {
                    runtime.inTransition = true;
                    runtime.targetStateId = transition.targetStateId;
                    runtime.targetStateTimeSeconds = 0.0f;
                    runtime.transitionElapsedSeconds = 0.0f;
                    runtime.transitionDurationSeconds = transition.durationSeconds;
                    runtime.hasRootMotionReference = false;
                    break;
                }
            }
        }

        const auto* activeState = layer.find_state(runtime.activeStateId);
        if (activeState == nullptr)
        {
            continue;
        }

        const float activeTime = runtime.stateTimeSeconds * std::max(activeState->playbackSpeed, 0.0f);
        NodeSample blended = sample_state_node(*activeState, *controller, clipAssetManager, component, activeTime);
        for (const auto& clip : blended.clips)
        {
            if (clip.clip != nullptr)
            {
                fire_clip_events(component, *clip.clip, activeTime, activeTime + (deltaTime * std::max(activeState->playbackSpeed, 0.0f)));
            }
        }

        if (runtime.inTransition)
        {
            if (const auto* targetState = layer.find_state(runtime.targetStateId); targetState != nullptr)
            {
                const float targetTime = runtime.targetStateTimeSeconds * std::max(targetState->playbackSpeed, 0.0f);
                NodeSample target = sample_state_node(*targetState, *controller, clipAssetManager, component, targetTime);
                for (const auto& clip : target.clips)
                {
                    if (clip.clip != nullptr)
                    {
                        fire_clip_events(component, *clip.clip, targetTime, targetTime + (deltaTime * std::max(targetState->playbackSpeed, 0.0f)));
                    }
                }

                const float alpha = runtime.transitionDurationSeconds <= 0.0f ? 1.0f : std::clamp(runtime.transitionElapsedSeconds / runtime.transitionDurationSeconds, 0.0f, 1.0f);
                VoxelAssemblyPose transitionPose{};
                std::unordered_set<std::string> partIds{};
                for (const auto& part : blended.pose.parts) partIds.insert(part.partId);
                for (const auto& part : target.pose.parts) partIds.insert(part.partId);

                for (const auto& partId : partIds)
                {
                    const auto* lhs = blended.pose.find_part(partId);
                    const auto* rhs = target.pose.find_part(partId);
                    VoxelAssemblyPosePart part{ .partId = partId };
                    if (lhs != nullptr && rhs != nullptr && lhs->localPosition.has_value() && rhs->localPosition.has_value()) part.localPosition = glm::mix(lhs->localPosition.value(), rhs->localPosition.value(), alpha);
                    else if (alpha < 0.5f && lhs != nullptr) part.localPosition = lhs->localPosition;
                    else if (rhs != nullptr) part.localPosition = rhs->localPosition;

                    if (lhs != nullptr && rhs != nullptr && lhs->localRotation.has_value() && rhs->localRotation.has_value()) part.localRotation = glm::slerp(lhs->localRotation.value(), rhs->localRotation.value(), alpha);
                    else if (alpha < 0.5f && lhs != nullptr) part.localRotation = lhs->localRotation;
                    else if (rhs != nullptr) part.localRotation = rhs->localRotation;

                    if (lhs != nullptr && rhs != nullptr && lhs->localScale.has_value() && rhs->localScale.has_value()) part.localScale = glm::mix(lhs->localScale.value(), rhs->localScale.value(), alpha);
                    else if (alpha < 0.5f && lhs != nullptr) part.localScale = lhs->localScale;
                    else if (rhs != nullptr) part.localScale = rhs->localScale;

                    part.visible = alpha < 0.5f ? (lhs != nullptr ? lhs->visible : std::optional<bool>{}) : (rhs != nullptr ? rhs->visible : std::optional<bool>{});
                    part.bindingStateId = alpha < 0.5f ? (lhs != nullptr ? lhs->bindingStateId : std::optional<std::string>{}) : (rhs != nullptr ? rhs->bindingStateId : std::optional<std::string>{});
                    if (part.localPosition.has_value() || part.localRotation.has_value() || part.localScale.has_value() || part.visible.has_value() || part.bindingStateId.has_value())
                    {
                        transitionPose.parts.push_back(std::move(part));
                    }
                }

                blended.pose = std::move(transitionPose);
                blended.rootMotionMode = alpha < 0.5f ? blended.rootMotionMode : target.rootMotionMode;
                if (blended.rootTransform.valid && target.rootTransform.valid)
                {
                    blended.rootTransform.position = glm::mix(blended.rootTransform.position, target.rootTransform.position, alpha);
                    blended.rootTransform.rotation = glm::slerp(blended.rootTransform.rotation, target.rootTransform.rotation, alpha);
                }
                else if (alpha >= 0.5f)
                {
                    blended.rootTransform = target.rootTransform;
                }
            }
        }

        const auto mask = layer_mask(assemblyAsset, layer);
        if (layer.blendMode == VoxelAnimationLayerBlendMode::Additive)
            apply_additive_pose(component.currentPose, blended.pose, assemblyAsset, assemblyComponent, mask, layer.weight);
        else
            apply_override_pose(component.currentPose, blended.pose, mask);

        if (layerIndex == 0)
        {
            std::string sourcePartId = assemblyAsset.rootPartId;
            if (!blended.clips.empty() && blended.clips.front().clip != nullptr && !blended.clips.front().clip->motionSourcePartId.empty())
            {
                sourcePartId = blended.clips.front().clip->motionSourcePartId;
            }
            if (!blended.rootTransform.valid && !sourcePartId.empty())
            {
                if (const auto* sourcePart = component.currentPose.find_part(sourcePartId); sourcePart != nullptr)
                {
                    blended.rootTransform.valid = sourcePart->localPosition.has_value() || sourcePart->localRotation.has_value();
                    blended.rootTransform.position = sourcePart->localPosition.value_or(glm::vec3(0.0f));
                    blended.rootTransform.rotation = sourcePart->localRotation.value_or(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                }
            }
            apply_root_motion(component, runtime, sourcePartId, blended.rootMotionMode, blended.rootTransform);
        }

        runtime.stateTimeSeconds += deltaTime;
        if (runtime.inTransition)
        {
            runtime.targetStateTimeSeconds += deltaTime;
            runtime.transitionElapsedSeconds += deltaTime;
            if (runtime.transitionElapsedSeconds >= runtime.transitionDurationSeconds)
            {
                runtime.activeStateId = runtime.targetStateId;
                runtime.stateTimeSeconds = runtime.targetStateTimeSeconds;
                runtime.inTransition = false;
                runtime.targetStateId.clear();
                runtime.targetStateTimeSeconds = 0.0f;
                runtime.transitionElapsedSeconds = 0.0f;
                runtime.transitionDurationSeconds = 0.0f;
                runtime.hasRootMotionReference = false;
            }
        }
    }

    for (auto& [parameterId, value] : component.triggerParameters)
    {
        (void)parameterId;
        value = false;
    }
}
