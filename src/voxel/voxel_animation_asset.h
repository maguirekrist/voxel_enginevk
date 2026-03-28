#pragma once

#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

#include "third_party/nlohmann/json.hpp"

enum class VoxelAnimationLoopMode
{
    Once,
    Loop
};

struct VoxelAnimationTransformKeyframe
{
    float timeSeconds{0.0f};
    glm::vec3 localPosition{0.0f};
    glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f};

    [[nodiscard]] bool operator==(const VoxelAnimationTransformKeyframe& other) const = default;
};

struct VoxelAnimationVisibilityKeyframe
{
    float timeSeconds{0.0f};
    bool visible{true};

    [[nodiscard]] bool operator==(const VoxelAnimationVisibilityKeyframe& other) const = default;
};

struct VoxelAnimationBindingStateKeyframe
{
    float timeSeconds{0.0f};
    std::string stateId{};

    [[nodiscard]] bool operator==(const VoxelAnimationBindingStateKeyframe& other) const = default;
};

struct VoxelAnimationEventKeyframe
{
    float timeSeconds{0.0f};
    std::string eventId{};
    nlohmann::json payload = nlohmann::json::object();

    [[nodiscard]] bool operator==(const VoxelAnimationEventKeyframe& other) const = default;
};

struct VoxelAnimationPartTrack
{
    std::string partId{};
    std::vector<VoxelAnimationTransformKeyframe> transformKeys{};
    std::vector<VoxelAnimationVisibilityKeyframe> visibilityKeys{};

    [[nodiscard]] bool operator==(const VoxelAnimationPartTrack& other) const = default;
};

struct VoxelAnimationBindingTrack
{
    std::string partId{};
    std::vector<VoxelAnimationBindingStateKeyframe> keys{};

    [[nodiscard]] bool operator==(const VoxelAnimationBindingTrack& other) const = default;
};

struct VoxelAnimationEventTrack
{
    std::string trackId{};
    std::vector<VoxelAnimationEventKeyframe> events{};

    [[nodiscard]] bool operator==(const VoxelAnimationEventTrack& other) const = default;
};

class VoxelAnimationClipAsset
{
public:
    std::string assetId{"untitled"};
    std::string displayName{"Untitled"};
    std::string assemblyAssetId{};
    float durationSeconds{1.0f};
    VoxelAnimationLoopMode loopMode{VoxelAnimationLoopMode::Loop};
    float frameRateHint{24.0f};
    std::string motionSourcePartId{};
    std::vector<VoxelAnimationPartTrack> partTracks{};
    std::vector<VoxelAnimationBindingTrack> bindingTracks{};
    std::vector<VoxelAnimationEventTrack> eventTracks{};

    [[nodiscard]] VoxelAnimationPartTrack* find_part_track(const std::string_view partId)
    {
        const auto it = std::ranges::find_if(partTracks, [&](const VoxelAnimationPartTrack& track)
        {
            return track.partId == partId;
        });
        return it != partTracks.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const VoxelAnimationPartTrack* find_part_track(const std::string_view partId) const
    {
        const auto it = std::ranges::find_if(partTracks, [&](const VoxelAnimationPartTrack& track)
        {
            return track.partId == partId;
        });
        return it != partTracks.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] VoxelAnimationBindingTrack* find_binding_track(const std::string_view partId)
    {
        const auto it = std::ranges::find_if(bindingTracks, [&](const VoxelAnimationBindingTrack& track)
        {
            return track.partId == partId;
        });
        return it != bindingTracks.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const VoxelAnimationBindingTrack* find_binding_track(const std::string_view partId) const
    {
        const auto it = std::ranges::find_if(bindingTracks, [&](const VoxelAnimationBindingTrack& track)
        {
            return track.partId == partId;
        });
        return it != bindingTracks.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] bool operator==(const VoxelAnimationClipAsset& other) const = default;
};

enum class VoxelAnimationParameterType
{
    Float,
    Bool,
    Trigger
};

struct VoxelAnimationParameterDefinition
{
    std::string parameterId{};
    std::string displayName{};
    VoxelAnimationParameterType type{VoxelAnimationParameterType::Float};
    float defaultFloatValue{0.0f};
    bool defaultBoolValue{false};

    [[nodiscard]] bool operator==(const VoxelAnimationParameterDefinition& other) const = default;
};

struct VoxelAnimationBlendSpaceSample
{
    float x{0.0f};
    float y{0.0f};
    std::string clipAssetId{};

    [[nodiscard]] bool operator==(const VoxelAnimationBlendSpaceSample& other) const = default;
};

struct VoxelAnimationBlendSpace2D
{
    std::string blendSpaceId{};
    std::string displayName{};
    std::string xParameterId{};
    std::string yParameterId{};
    std::vector<VoxelAnimationBlendSpaceSample> samples{};

    [[nodiscard]] bool operator==(const VoxelAnimationBlendSpace2D& other) const = default;
};

enum class VoxelAnimationConditionOp
{
    Greater,
    Less,
    GreaterEqual,
    LessEqual,
    Equal,
    NotEqual,
    IsTrue,
    IsFalse,
    Triggered
};

struct VoxelAnimationCondition
{
    std::string parameterId{};
    VoxelAnimationConditionOp op{VoxelAnimationConditionOp::GreaterEqual};
    float value{0.0f};

    [[nodiscard]] bool operator==(const VoxelAnimationCondition& other) const = default;
};

enum class VoxelAnimationLayerBlendMode
{
    Override,
    Additive
};

enum class VoxelAnimationStateNodeType
{
    ClipPlayer,
    BlendSpace2D
};

enum class RootMotionMode
{
    Ignore,
    ExtractPlanar,
    ExtractFull
};

struct VoxelAnimationStateDefinition
{
    std::string stateId{};
    std::string displayName{};
    VoxelAnimationStateNodeType nodeType{VoxelAnimationStateNodeType::ClipPlayer};
    std::string clipAssetId{};
    std::string blendSpaceId{};
    float playbackSpeed{1.0f};
    RootMotionMode rootMotionMode{RootMotionMode::Ignore};

    [[nodiscard]] bool operator==(const VoxelAnimationStateDefinition& other) const = default;
};

struct VoxelAnimationTransitionDefinition
{
    std::string sourceStateId{};
    std::string targetStateId{};
    float durationSeconds{0.12f};
    bool requiresExitTime{false};
    float exitTimeNormalized{1.0f};
    std::vector<VoxelAnimationCondition> conditions{};

    [[nodiscard]] bool operator==(const VoxelAnimationTransitionDefinition& other) const = default;
};

struct VoxelAnimationLayerDefinition
{
    std::string layerId{};
    std::string displayName{};
    VoxelAnimationLayerBlendMode blendMode{VoxelAnimationLayerBlendMode::Override};
    float weight{1.0f};
    std::string entryStateId{};
    std::vector<std::string> maskedPartIds{};
    bool includeMaskedPartDescendants{true};
    std::vector<VoxelAnimationStateDefinition> states{};
    std::vector<VoxelAnimationTransitionDefinition> transitions{};

    [[nodiscard]] const VoxelAnimationStateDefinition* find_state(const std::string_view stateId) const
    {
        const auto it = std::ranges::find_if(states, [&](const VoxelAnimationStateDefinition& state)
        {
            return state.stateId == stateId;
        });
        return it != states.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] bool operator==(const VoxelAnimationLayerDefinition& other) const = default;
};

class VoxelAnimationControllerAsset
{
public:
    std::string assetId{"untitled"};
    std::string displayName{"Untitled"};
    std::string assemblyAssetId{};
    std::vector<VoxelAnimationParameterDefinition> parameters{};
    std::vector<VoxelAnimationBlendSpace2D> blendSpaces{};
    std::vector<VoxelAnimationLayerDefinition> layers{};

    [[nodiscard]] VoxelAnimationParameterDefinition* find_parameter(const std::string_view parameterId)
    {
        const auto it = std::ranges::find_if(parameters, [&](const VoxelAnimationParameterDefinition& parameter)
        {
            return parameter.parameterId == parameterId;
        });
        return it != parameters.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const VoxelAnimationParameterDefinition* find_parameter(const std::string_view parameterId) const
    {
        const auto it = std::ranges::find_if(parameters, [&](const VoxelAnimationParameterDefinition& parameter)
        {
            return parameter.parameterId == parameterId;
        });
        return it != parameters.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] VoxelAnimationBlendSpace2D* find_blend_space(const std::string_view blendSpaceId)
    {
        const auto it = std::ranges::find_if(blendSpaces, [&](const VoxelAnimationBlendSpace2D& blendSpace)
        {
            return blendSpace.blendSpaceId == blendSpaceId;
        });
        return it != blendSpaces.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const VoxelAnimationBlendSpace2D* find_blend_space(const std::string_view blendSpaceId) const
    {
        const auto it = std::ranges::find_if(blendSpaces, [&](const VoxelAnimationBlendSpace2D& blendSpace)
        {
            return blendSpace.blendSpaceId == blendSpaceId;
        });
        return it != blendSpaces.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] VoxelAnimationLayerDefinition* find_layer(const std::string_view layerId)
    {
        const auto it = std::ranges::find_if(layers, [&](const VoxelAnimationLayerDefinition& layer)
        {
            return layer.layerId == layerId;
        });
        return it != layers.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const VoxelAnimationLayerDefinition* find_layer(const std::string_view layerId) const
    {
        const auto it = std::ranges::find_if(layers, [&](const VoxelAnimationLayerDefinition& layer)
        {
            return layer.layerId == layerId;
        });
        return it != layers.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] bool operator==(const VoxelAnimationControllerAsset& other) const = default;
};
