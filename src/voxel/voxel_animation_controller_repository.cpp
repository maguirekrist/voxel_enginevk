#include "voxel_animation_controller_repository.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>

namespace
{
    constexpr int VoxelAnimationControllerVersion = 1;
    constexpr std::string_view VoxelAnimationControllerFileSuffix = ".vxanimc.json";

    std::string sanitize_asset_id(const std::string_view rawAssetId)
    {
        std::string result{};
        result.reserve(rawAssetId.size());
        for (const char ch : rawAssetId)
        {
            if ((ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') ||
                ch == '_' ||
                ch == '-')
            {
                result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
        }

        if (result.empty())
        {
            result = "untitled";
        }
        return result;
    }

    std::string_view parameter_type_to_string(const VoxelAnimationParameterType type)
    {
        switch (type)
        {
        case VoxelAnimationParameterType::Bool:
            return "bool";
        case VoxelAnimationParameterType::Trigger:
            return "trigger";
        case VoxelAnimationParameterType::Float:
        default:
            return "float";
        }
    }

    VoxelAnimationParameterType parameter_type_from_string(const std::string_view value)
    {
        if (value == "bool")
        {
            return VoxelAnimationParameterType::Bool;
        }
        if (value == "trigger")
        {
            return VoxelAnimationParameterType::Trigger;
        }
        return VoxelAnimationParameterType::Float;
    }

    std::string_view blend_mode_to_string(const VoxelAnimationLayerBlendMode mode)
    {
        switch (mode)
        {
        case VoxelAnimationLayerBlendMode::Additive:
            return "additive";
        case VoxelAnimationLayerBlendMode::Override:
        default:
            return "override";
        }
    }

    VoxelAnimationLayerBlendMode blend_mode_from_string(const std::string_view value)
    {
        if (value == "additive")
        {
            return VoxelAnimationLayerBlendMode::Additive;
        }
        return VoxelAnimationLayerBlendMode::Override;
    }

    std::string_view state_type_to_string(const VoxelAnimationStateNodeType type)
    {
        switch (type)
        {
        case VoxelAnimationStateNodeType::BlendSpace2D:
            return "blend_space_2d";
        case VoxelAnimationStateNodeType::ClipPlayer:
        default:
            return "clip_player";
        }
    }

    VoxelAnimationStateNodeType state_type_from_string(const std::string_view value)
    {
        if (value == "blend_space_2d")
        {
            return VoxelAnimationStateNodeType::BlendSpace2D;
        }
        return VoxelAnimationStateNodeType::ClipPlayer;
    }

    std::string_view condition_op_to_string(const VoxelAnimationConditionOp op)
    {
        switch (op)
        {
        case VoxelAnimationConditionOp::Greater:
            return "greater";
        case VoxelAnimationConditionOp::Less:
            return "less";
        case VoxelAnimationConditionOp::LessEqual:
            return "less_equal";
        case VoxelAnimationConditionOp::Equal:
            return "equal";
        case VoxelAnimationConditionOp::NotEqual:
            return "not_equal";
        case VoxelAnimationConditionOp::IsTrue:
            return "is_true";
        case VoxelAnimationConditionOp::IsFalse:
            return "is_false";
        case VoxelAnimationConditionOp::Triggered:
            return "triggered";
        case VoxelAnimationConditionOp::GreaterEqual:
        default:
            return "greater_equal";
        }
    }

    VoxelAnimationConditionOp condition_op_from_string(const std::string_view value)
    {
        if (value == "greater")
        {
            return VoxelAnimationConditionOp::Greater;
        }
        if (value == "less")
        {
            return VoxelAnimationConditionOp::Less;
        }
        if (value == "less_equal")
        {
            return VoxelAnimationConditionOp::LessEqual;
        }
        if (value == "equal")
        {
            return VoxelAnimationConditionOp::Equal;
        }
        if (value == "not_equal")
        {
            return VoxelAnimationConditionOp::NotEqual;
        }
        if (value == "is_true")
        {
            return VoxelAnimationConditionOp::IsTrue;
        }
        if (value == "is_false")
        {
            return VoxelAnimationConditionOp::IsFalse;
        }
        if (value == "triggered")
        {
            return VoxelAnimationConditionOp::Triggered;
        }
        return VoxelAnimationConditionOp::GreaterEqual;
    }

    std::string_view root_motion_mode_to_string(const RootMotionMode mode)
    {
        switch (mode)
        {
        case RootMotionMode::ExtractPlanar:
            return "extract_planar";
        case RootMotionMode::ExtractFull:
            return "extract_full";
        case RootMotionMode::Ignore:
        default:
            return "ignore";
        }
    }

    RootMotionMode root_motion_mode_from_string(const std::string_view value)
    {
        if (value == "extract_planar")
        {
            return RootMotionMode::ExtractPlanar;
        }
        if (value == "extract_full")
        {
            return RootMotionMode::ExtractFull;
        }
        return RootMotionMode::Ignore;
    }

    nlohmann::json serialize(const VoxelAnimationControllerAsset& asset)
    {
        nlohmann::json parameters = nlohmann::json::array();
        for (const VoxelAnimationParameterDefinition& parameter : asset.parameters)
        {
            parameters.push_back({
                { "parameterId", parameter.parameterId },
                { "displayName", parameter.displayName },
                { "type", parameter_type_to_string(parameter.type) },
                { "defaultFloatValue", parameter.defaultFloatValue },
                { "defaultBoolValue", parameter.defaultBoolValue }
            });
        }

        nlohmann::json blendSpaces = nlohmann::json::array();
        for (const VoxelAnimationBlendSpace2D& blendSpace : asset.blendSpaces)
        {
            nlohmann::json samples = nlohmann::json::array();
            for (const VoxelAnimationBlendSpaceSample& sample : blendSpace.samples)
            {
                samples.push_back({
                    { "x", sample.x },
                    { "y", sample.y },
                    { "clipAssetId", sample.clipAssetId }
                });
            }

            blendSpaces.push_back({
                { "blendSpaceId", blendSpace.blendSpaceId },
                { "displayName", blendSpace.displayName },
                { "xParameterId", blendSpace.xParameterId },
                { "yParameterId", blendSpace.yParameterId },
                { "samples", std::move(samples) }
            });
        }

        nlohmann::json layers = nlohmann::json::array();
        for (const VoxelAnimationLayerDefinition& layer : asset.layers)
        {
            nlohmann::json maskedPartIds = nlohmann::json::array();
            for (const std::string& partId : layer.maskedPartIds)
            {
                maskedPartIds.push_back(partId);
            }

            nlohmann::json states = nlohmann::json::array();
            for (const VoxelAnimationStateDefinition& state : layer.states)
            {
                states.push_back({
                    { "stateId", state.stateId },
                    { "displayName", state.displayName },
                    { "nodeType", state_type_to_string(state.nodeType) },
                    { "clipAssetId", state.clipAssetId },
                    { "blendSpaceId", state.blendSpaceId },
                    { "playbackSpeed", state.playbackSpeed },
                    { "rootMotionMode", root_motion_mode_to_string(state.rootMotionMode) }
                });
            }

            nlohmann::json transitions = nlohmann::json::array();
            for (const VoxelAnimationTransitionDefinition& transition : layer.transitions)
            {
                nlohmann::json conditions = nlohmann::json::array();
                for (const VoxelAnimationCondition& condition : transition.conditions)
                {
                    conditions.push_back({
                        { "parameterId", condition.parameterId },
                        { "op", condition_op_to_string(condition.op) },
                        { "value", condition.value }
                    });
                }

                transitions.push_back({
                    { "sourceStateId", transition.sourceStateId },
                    { "targetStateId", transition.targetStateId },
                    { "durationSeconds", transition.durationSeconds },
                    { "requiresExitTime", transition.requiresExitTime },
                    { "exitTimeNormalized", transition.exitTimeNormalized },
                    { "conditions", std::move(conditions) }
                });
            }

            layers.push_back({
                { "layerId", layer.layerId },
                { "displayName", layer.displayName },
                { "blendMode", blend_mode_to_string(layer.blendMode) },
                { "weight", layer.weight },
                { "entryStateId", layer.entryStateId },
                { "maskedPartIds", std::move(maskedPartIds) },
                { "includeMaskedPartDescendants", layer.includeMaskedPartDescendants },
                { "states", std::move(states) },
                { "transitions", std::move(transitions) }
            });
        }

        return {
            { "version", VoxelAnimationControllerVersion },
            { "assetId", asset.assetId },
            { "displayName", asset.displayName },
            { "assemblyAssetId", asset.assemblyAssetId },
            { "parameters", std::move(parameters) },
            { "blendSpaces", std::move(blendSpaces) },
            { "layers", std::move(layers) }
        };
    }

    VoxelAnimationControllerAsset deserialize(const nlohmann::json& document)
    {
        VoxelAnimationControllerAsset asset{};
        asset.assetId = sanitize_asset_id(document.value("assetId", asset.assetId));
        asset.displayName = document.value("displayName", asset.displayName);
        asset.assemblyAssetId = document.value("assemblyAssetId", asset.assemblyAssetId);

        if (document.contains("parameters") && document.at("parameters").is_array())
        {
            for (const auto& parameterNode : document.at("parameters"))
            {
                if (!parameterNode.is_object())
                {
                    continue;
                }

                VoxelAnimationParameterDefinition parameter{};
                parameter.parameterId = parameterNode.value("parameterId", parameter.parameterId);
                parameter.displayName = parameterNode.value("displayName", parameter.displayName);
                parameter.type = parameter_type_from_string(parameterNode.value("type", std::string(parameter_type_to_string(parameter.type))));
                parameter.defaultFloatValue = parameterNode.value("defaultFloatValue", parameter.defaultFloatValue);
                parameter.defaultBoolValue = parameterNode.value("defaultBoolValue", parameter.defaultBoolValue);
                if (!parameter.parameterId.empty())
                {
                    asset.parameters.push_back(std::move(parameter));
                }
            }
        }

        if (document.contains("blendSpaces") && document.at("blendSpaces").is_array())
        {
            for (const auto& blendNode : document.at("blendSpaces"))
            {
                if (!blendNode.is_object())
                {
                    continue;
                }

                VoxelAnimationBlendSpace2D blendSpace{};
                blendSpace.blendSpaceId = blendNode.value("blendSpaceId", blendSpace.blendSpaceId);
                blendSpace.displayName = blendNode.value("displayName", blendSpace.displayName);
                blendSpace.xParameterId = blendNode.value("xParameterId", blendSpace.xParameterId);
                blendSpace.yParameterId = blendNode.value("yParameterId", blendSpace.yParameterId);
                if (blendNode.contains("samples") && blendNode.at("samples").is_array())
                {
                    for (const auto& sampleNode : blendNode.at("samples"))
                    {
                        if (!sampleNode.is_object())
                        {
                            continue;
                        }

                        VoxelAnimationBlendSpaceSample sample{};
                        sample.x = sampleNode.value("x", sample.x);
                        sample.y = sampleNode.value("y", sample.y);
                        sample.clipAssetId = sampleNode.value("clipAssetId", sample.clipAssetId);
                        if (!sample.clipAssetId.empty())
                        {
                            blendSpace.samples.push_back(std::move(sample));
                        }
                    }
                }
                if (!blendSpace.blendSpaceId.empty())
                {
                    asset.blendSpaces.push_back(std::move(blendSpace));
                }
            }
        }

        if (document.contains("layers") && document.at("layers").is_array())
        {
            for (const auto& layerNode : document.at("layers"))
            {
                if (!layerNode.is_object())
                {
                    continue;
                }

                VoxelAnimationLayerDefinition layer{};
                layer.layerId = layerNode.value("layerId", layer.layerId);
                layer.displayName = layerNode.value("displayName", layer.displayName);
                layer.blendMode = blend_mode_from_string(layerNode.value("blendMode", std::string(blend_mode_to_string(layer.blendMode))));
                layer.weight = layerNode.value("weight", layer.weight);
                layer.entryStateId = layerNode.value("entryStateId", layer.entryStateId);
                layer.includeMaskedPartDescendants = layerNode.value("includeMaskedPartDescendants", layer.includeMaskedPartDescendants);
                if (layerNode.contains("maskedPartIds") && layerNode.at("maskedPartIds").is_array())
                {
                    for (const auto& partNode : layerNode.at("maskedPartIds"))
                    {
                        if (partNode.is_string())
                        {
                            layer.maskedPartIds.push_back(partNode.get<std::string>());
                        }
                    }
                }

                if (layerNode.contains("states") && layerNode.at("states").is_array())
                {
                    for (const auto& stateNode : layerNode.at("states"))
                    {
                        if (!stateNode.is_object())
                        {
                            continue;
                        }

                        VoxelAnimationStateDefinition state{};
                        state.stateId = stateNode.value("stateId", state.stateId);
                        state.displayName = stateNode.value("displayName", state.displayName);
                        state.nodeType = state_type_from_string(stateNode.value("nodeType", std::string(state_type_to_string(state.nodeType))));
                        state.clipAssetId = stateNode.value("clipAssetId", state.clipAssetId);
                        state.blendSpaceId = stateNode.value("blendSpaceId", state.blendSpaceId);
                        state.playbackSpeed = stateNode.value("playbackSpeed", state.playbackSpeed);
                        state.rootMotionMode = root_motion_mode_from_string(stateNode.value("rootMotionMode", std::string(root_motion_mode_to_string(state.rootMotionMode))));
                        if (!state.stateId.empty())
                        {
                            layer.states.push_back(std::move(state));
                        }
                    }
                }

                if (layerNode.contains("transitions") && layerNode.at("transitions").is_array())
                {
                    for (const auto& transitionNode : layerNode.at("transitions"))
                    {
                        if (!transitionNode.is_object())
                        {
                            continue;
                        }

                        VoxelAnimationTransitionDefinition transition{};
                        transition.sourceStateId = transitionNode.value("sourceStateId", transition.sourceStateId);
                        transition.targetStateId = transitionNode.value("targetStateId", transition.targetStateId);
                        transition.durationSeconds = std::max(0.0f, transitionNode.value("durationSeconds", transition.durationSeconds));
                        transition.requiresExitTime = transitionNode.value("requiresExitTime", transition.requiresExitTime);
                        transition.exitTimeNormalized = transitionNode.value("exitTimeNormalized", transition.exitTimeNormalized);
                        if (transitionNode.contains("conditions") && transitionNode.at("conditions").is_array())
                        {
                            for (const auto& conditionNode : transitionNode.at("conditions"))
                            {
                                if (!conditionNode.is_object())
                                {
                                    continue;
                                }

                                VoxelAnimationCondition condition{};
                                condition.parameterId = conditionNode.value("parameterId", condition.parameterId);
                                condition.op = condition_op_from_string(conditionNode.value("op", std::string(condition_op_to_string(condition.op))));
                                condition.value = conditionNode.value("value", condition.value);
                                transition.conditions.push_back(std::move(condition));
                            }
                        }
                        if (!transition.sourceStateId.empty() && !transition.targetStateId.empty())
                        {
                            layer.transitions.push_back(std::move(transition));
                        }
                    }
                }

                if (!layer.layerId.empty())
                {
                    asset.layers.push_back(std::move(layer));
                }
            }
        }

        return asset;
    }
}

VoxelAnimationControllerRepository::VoxelAnimationControllerRepository(
    const config::IJsonDocumentStore& documentStore,
    std::filesystem::path rootPath) :
    _documentStore(documentStore),
    _rootPath(std::move(rootPath))
{
}

std::optional<VoxelAnimationControllerAsset> VoxelAnimationControllerRepository::load(const std::string_view assetId) const
{
    try
    {
        if (const auto document = _documentStore.load(resolve_path(assetId)); document.has_value())
        {
            return deserialize(document.value());
        }
    }
    catch (const std::exception&)
    {
    }

    return std::nullopt;
}

std::vector<std::string> VoxelAnimationControllerRepository::list_asset_ids() const
{
    std::vector<std::string> assetIds{};
    try
    {
        if (!std::filesystem::exists(_rootPath))
        {
            return assetIds;
        }

        for (const auto& entry : std::filesystem::directory_iterator(_rootPath))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const std::string filename = entry.path().filename().string();
            if (!filename.ends_with(VoxelAnimationControllerFileSuffix))
            {
                continue;
            }

            const std::string_view filenameView{filename};
            assetIds.push_back(std::string(filenameView.substr(0, filenameView.size() - VoxelAnimationControllerFileSuffix.size())));
        }

        std::ranges::sort(assetIds);
    }
    catch (const std::exception&)
    {
    }

    return assetIds;
}

void VoxelAnimationControllerRepository::save(const VoxelAnimationControllerAsset& asset) const
{
    if (asset.assemblyAssetId.empty())
    {
        throw std::runtime_error("VoxelAnimationControllerRepository::save: assemblyAssetId must not be empty");
    }

    VoxelAnimationControllerAsset normalized = asset;
    normalized.assetId = sanitize_asset_id(asset.assetId);
    if (normalized.displayName.empty())
    {
        normalized.displayName = normalized.assetId;
    }

    _documentStore.save(resolve_path(normalized.assetId), serialize(normalized));
}

bool VoxelAnimationControllerRepository::remove(const std::string_view assetId) const
{
    std::error_code error{};
    return std::filesystem::remove(resolve_path(assetId), error);
}

std::filesystem::path VoxelAnimationControllerRepository::resolve_path(const std::string_view assetId) const
{
    return _rootPath / std::format("{}.vxanimc.json", sanitize_asset_id(assetId));
}

const std::filesystem::path& VoxelAnimationControllerRepository::root_path() const noexcept
{
    return _rootPath;
}
