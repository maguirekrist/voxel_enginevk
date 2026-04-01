#include "voxel_animation_clip_repository.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>

namespace
{
    constexpr int VoxelAnimationClipVersion = 1;
    constexpr std::string_view VoxelAnimationClipFileSuffix = ".vxanim.json";

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

    nlohmann::json vec3_to_json(const glm::vec3& value)
    {
        return {
            { "x", value.x },
            { "y", value.y },
            { "z", value.z }
        };
    }

    glm::vec3 vec3_from_json(const nlohmann::json& node, const glm::vec3 fallback = glm::vec3(0.0f))
    {
        return glm::vec3(
            node.value("x", fallback.x),
            node.value("y", fallback.y),
            node.value("z", fallback.z));
    }

    nlohmann::json quat_to_json(const glm::quat& value)
    {
        return {
            { "x", value.x },
            { "y", value.y },
            { "z", value.z },
            { "w", value.w }
        };
    }

    glm::quat quat_from_json(const nlohmann::json& node, const glm::quat fallback = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
    {
        return glm::quat(
            node.value("w", fallback.w),
            node.value("x", fallback.x),
            node.value("y", fallback.y),
            node.value("z", fallback.z));
    }

    std::string_view loop_mode_to_string(const VoxelAnimationLoopMode loopMode)
    {
        switch (loopMode)
        {
        case VoxelAnimationLoopMode::Once:
            return "once";
        case VoxelAnimationLoopMode::Loop:
        default:
            return "loop";
        }
    }

    VoxelAnimationLoopMode loop_mode_from_string(const std::string_view value)
    {
        if (value == "once")
        {
            return VoxelAnimationLoopMode::Once;
        }
        return VoxelAnimationLoopMode::Loop;
    }

    nlohmann::json serialize_transform_key(const VoxelAnimationTransformKeyframe& key)
    {
        return {
            { "timeSeconds", key.timeSeconds },
            { "localPosition", vec3_to_json(key.localPosition) },
            { "localRotation", quat_to_json(key.localRotation) },
            { "localScale", vec3_to_json(key.localScale) }
        };
    }

    VoxelAnimationTransformKeyframe deserialize_transform_key(const nlohmann::json& node)
    {
        VoxelAnimationTransformKeyframe key{};
        key.timeSeconds = node.value("timeSeconds", key.timeSeconds);
        if (node.contains("localPosition") && node.at("localPosition").is_object())
        {
            key.localPosition = vec3_from_json(node.at("localPosition"), key.localPosition);
        }
        if (node.contains("localRotation") && node.at("localRotation").is_object())
        {
            key.localRotation = quat_from_json(node.at("localRotation"), key.localRotation);
        }
        if (node.contains("localScale") && node.at("localScale").is_object())
        {
            key.localScale = vec3_from_json(node.at("localScale"), key.localScale);
        }
        return key;
    }

    nlohmann::json serialize_visibility_key(const VoxelAnimationVisibilityKeyframe& key)
    {
        return {
            { "timeSeconds", key.timeSeconds },
            { "visible", key.visible }
        };
    }

    VoxelAnimationVisibilityKeyframe deserialize_visibility_key(const nlohmann::json& node)
    {
        VoxelAnimationVisibilityKeyframe key{};
        key.timeSeconds = node.value("timeSeconds", key.timeSeconds);
        key.visible = node.value("visible", key.visible);
        return key;
    }

    nlohmann::json serialize_binding_key(const VoxelAnimationBindingStateKeyframe& key)
    {
        return {
            { "timeSeconds", key.timeSeconds },
            { "stateId", key.stateId }
        };
    }

    VoxelAnimationBindingStateKeyframe deserialize_binding_key(const nlohmann::json& node)
    {
        VoxelAnimationBindingStateKeyframe key{};
        key.timeSeconds = node.value("timeSeconds", key.timeSeconds);
        key.stateId = node.value("stateId", key.stateId);
        return key;
    }

    nlohmann::json serialize_event_key(const VoxelAnimationEventKeyframe& key)
    {
        return {
            { "timeSeconds", key.timeSeconds },
            { "eventId", key.eventId },
            { "payload", key.payload }
        };
    }

    VoxelAnimationEventKeyframe deserialize_event_key(const nlohmann::json& node)
    {
        VoxelAnimationEventKeyframe key{};
        key.timeSeconds = node.value("timeSeconds", key.timeSeconds);
        key.eventId = node.value("eventId", key.eventId);
        if (node.contains("payload"))
        {
            key.payload = node.at("payload");
        }
        return key;
    }

    nlohmann::json serialize(const VoxelAnimationClipAsset& asset)
    {
        nlohmann::json partTracks = nlohmann::json::array();
        for (const VoxelAnimationPartTrack& track : asset.partTracks)
        {
            nlohmann::json transformKeys = nlohmann::json::array();
            for (const VoxelAnimationTransformKeyframe& key : track.transformKeys)
            {
                transformKeys.push_back(serialize_transform_key(key));
            }

            nlohmann::json visibilityKeys = nlohmann::json::array();
            for (const VoxelAnimationVisibilityKeyframe& key : track.visibilityKeys)
            {
                visibilityKeys.push_back(serialize_visibility_key(key));
            }

            partTracks.push_back({
                { "partId", track.partId },
                { "transformKeys", std::move(transformKeys) },
                { "visibilityKeys", std::move(visibilityKeys) }
            });
        }

        nlohmann::json bindingTracks = nlohmann::json::array();
        for (const VoxelAnimationBindingTrack& track : asset.bindingTracks)
        {
            nlohmann::json keys = nlohmann::json::array();
            for (const VoxelAnimationBindingStateKeyframe& key : track.keys)
            {
                keys.push_back(serialize_binding_key(key));
            }

            bindingTracks.push_back({
                { "partId", track.partId },
                { "keys", std::move(keys) }
            });
        }

        nlohmann::json eventTracks = nlohmann::json::array();
        for (const VoxelAnimationEventTrack& track : asset.eventTracks)
        {
            nlohmann::json keys = nlohmann::json::array();
            for (const VoxelAnimationEventKeyframe& key : track.events)
            {
                keys.push_back(serialize_event_key(key));
            }

            eventTracks.push_back({
                { "trackId", track.trackId },
                { "events", std::move(keys) }
            });
        }

        return {
            { "version", VoxelAnimationClipVersion },
            { "assetId", asset.assetId },
            { "displayName", asset.displayName },
            { "assemblyAssetId", asset.assemblyAssetId },
            { "durationSeconds", asset.durationSeconds },
            { "loopMode", loop_mode_to_string(asset.loopMode) },
            { "frameRateHint", asset.frameRateHint },
            { "motionSourcePartId", asset.motionSourcePartId },
            { "partTracks", std::move(partTracks) },
            { "bindingTracks", std::move(bindingTracks) },
            { "eventTracks", std::move(eventTracks) }
        };
    }

    VoxelAnimationClipAsset deserialize(const nlohmann::json& document)
    {
        VoxelAnimationClipAsset asset{};
        asset.assetId = sanitize_asset_id(document.value("assetId", asset.assetId));
        asset.displayName = document.value("displayName", asset.displayName);
        asset.assemblyAssetId = document.value("assemblyAssetId", asset.assemblyAssetId);
        asset.durationSeconds = std::max(0.001f, document.value("durationSeconds", asset.durationSeconds));
        asset.loopMode = loop_mode_from_string(document.value("loopMode", std::string(loop_mode_to_string(asset.loopMode))));
        asset.frameRateHint = std::max(1.0f, document.value("frameRateHint", asset.frameRateHint));
        asset.motionSourcePartId = document.value("motionSourcePartId", asset.motionSourcePartId);

        if (document.contains("partTracks") && document.at("partTracks").is_array())
        {
            for (const auto& trackNode : document.at("partTracks"))
            {
                if (!trackNode.is_object())
                {
                    continue;
                }

                VoxelAnimationPartTrack track{};
                track.partId = trackNode.value("partId", track.partId);
                if (trackNode.contains("transformKeys") && trackNode.at("transformKeys").is_array())
                {
                    for (const auto& keyNode : trackNode.at("transformKeys"))
                    {
                        if (keyNode.is_object())
                        {
                            track.transformKeys.push_back(deserialize_transform_key(keyNode));
                        }
                    }
                }
                if (trackNode.contains("visibilityKeys") && trackNode.at("visibilityKeys").is_array())
                {
                    for (const auto& keyNode : trackNode.at("visibilityKeys"))
                    {
                        if (keyNode.is_object())
                        {
                            track.visibilityKeys.push_back(deserialize_visibility_key(keyNode));
                        }
                    }
                }
                if (!track.partId.empty())
                {
                    asset.partTracks.push_back(std::move(track));
                }
            }
        }

        if (document.contains("bindingTracks") && document.at("bindingTracks").is_array())
        {
            for (const auto& trackNode : document.at("bindingTracks"))
            {
                if (!trackNode.is_object())
                {
                    continue;
                }

                VoxelAnimationBindingTrack track{};
                track.partId = trackNode.value("partId", track.partId);
                if (trackNode.contains("keys") && trackNode.at("keys").is_array())
                {
                    for (const auto& keyNode : trackNode.at("keys"))
                    {
                        if (keyNode.is_object())
                        {
                            track.keys.push_back(deserialize_binding_key(keyNode));
                        }
                    }
                }
                if (!track.partId.empty())
                {
                    asset.bindingTracks.push_back(std::move(track));
                }
            }
        }

        if (document.contains("eventTracks") && document.at("eventTracks").is_array())
        {
            for (const auto& trackNode : document.at("eventTracks"))
            {
                if (!trackNode.is_object())
                {
                    continue;
                }

                VoxelAnimationEventTrack track{};
                track.trackId = trackNode.value("trackId", track.trackId);
                if (trackNode.contains("events") && trackNode.at("events").is_array())
                {
                    for (const auto& keyNode : trackNode.at("events"))
                    {
                        if (keyNode.is_object())
                        {
                            track.events.push_back(deserialize_event_key(keyNode));
                        }
                    }
                }
                if (!track.trackId.empty())
                {
                    asset.eventTracks.push_back(std::move(track));
                }
            }
        }

        return asset;
    }
}

VoxelAnimationClipRepository::VoxelAnimationClipRepository(
    const config::IJsonDocumentStore& documentStore,
    std::filesystem::path rootPath) :
    _documentStore(documentStore),
    _rootPath(std::move(rootPath))
{
}

std::optional<VoxelAnimationClipAsset> VoxelAnimationClipRepository::load(const std::string_view assetId) const
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

std::vector<std::string> VoxelAnimationClipRepository::list_asset_ids() const
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
            if (!filename.ends_with(VoxelAnimationClipFileSuffix))
            {
                continue;
            }

            const std::string_view filenameView{filename};
            assetIds.push_back(std::string(filenameView.substr(0, filenameView.size() - VoxelAnimationClipFileSuffix.size())));
        }

        std::ranges::sort(assetIds);
    }
    catch (const std::exception&)
    {
    }

    return assetIds;
}

void VoxelAnimationClipRepository::save(const VoxelAnimationClipAsset& asset) const
{
    if (asset.assemblyAssetId.empty())
    {
        throw std::runtime_error("VoxelAnimationClipRepository::save: assemblyAssetId must not be empty");
    }

    VoxelAnimationClipAsset normalized = asset;
    normalized.assetId = sanitize_asset_id(asset.assetId);
    normalized.durationSeconds = std::max(0.001f, asset.durationSeconds);
    if (normalized.displayName.empty())
    {
        normalized.displayName = normalized.assetId;
    }

    _documentStore.save(resolve_path(normalized.assetId), serialize(normalized));
}

bool VoxelAnimationClipRepository::remove(const std::string_view assetId) const
{
    std::error_code error{};
    return std::filesystem::remove(resolve_path(assetId), error);
}

std::filesystem::path VoxelAnimationClipRepository::resolve_path(const std::string_view assetId) const
{
    return _rootPath / std::format("{}.vxanim.json", sanitize_asset_id(assetId));
}

const std::filesystem::path& VoxelAnimationClipRepository::root_path() const noexcept
{
    return _rootPath;
}
