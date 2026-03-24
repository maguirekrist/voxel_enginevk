#include "voxel_assembly_repository.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>

namespace
{
    constexpr int VoxelAssemblyVersion = 1;
    constexpr std::string_view VoxelAssemblyFileSuffix = ".vxma.json";

    std::string sanitize_asset_id(std::string_view rawAssetId)
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

    glm::vec3 vec3_from_json(const nlohmann::json& node, const glm::vec3 fallback)
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

    glm::quat quat_from_json(const nlohmann::json& node, const glm::quat fallback)
    {
        return glm::quat(
            node.value("w", fallback.w),
            node.value("x", fallback.x),
            node.value("y", fallback.y),
            node.value("z", fallback.z));
    }

    nlohmann::json binding_state_to_json(const VoxelAssemblyBindingState& bindingState)
    {
        return {
            { "stateId", bindingState.stateId },
            { "parentPartId", bindingState.parentPartId },
            { "parentAttachmentName", bindingState.parentAttachmentName },
            { "localPositionOffset", vec3_to_json(bindingState.localPositionOffset) },
            { "localRotationOffset", quat_to_json(bindingState.localRotationOffset) },
            { "localScale", vec3_to_json(bindingState.localScale) },
            { "visible", bindingState.visible }
        };
    }

    VoxelAssemblyBindingState binding_state_from_json(const nlohmann::json& node)
    {
        VoxelAssemblyBindingState bindingState{};
        bindingState.stateId = node.value("stateId", bindingState.stateId);
        bindingState.parentPartId = node.value("parentPartId", bindingState.parentPartId);
        bindingState.parentAttachmentName = node.value("parentAttachmentName", bindingState.parentAttachmentName);
        bindingState.visible = node.value("visible", bindingState.visible);

        if (node.contains("localPositionOffset") && node.at("localPositionOffset").is_object())
        {
            bindingState.localPositionOffset = vec3_from_json(
                node.at("localPositionOffset"),
                bindingState.localPositionOffset);
        }

        if (node.contains("localRotationOffset") && node.at("localRotationOffset").is_object())
        {
            bindingState.localRotationOffset = quat_from_json(
                node.at("localRotationOffset"),
                bindingState.localRotationOffset);
        }

        if (node.contains("localScale") && node.at("localScale").is_object())
        {
            bindingState.localScale = vec3_from_json(node.at("localScale"), bindingState.localScale);
        }

        return bindingState;
    }

    nlohmann::json part_definition_to_json(const VoxelAssemblyPartDefinition& part)
    {
        nlohmann::json states = nlohmann::json::array();
        for (const VoxelAssemblyBindingState& bindingState : part.bindingStates)
        {
            states.push_back(binding_state_to_json(bindingState));
        }

        nlohmann::json result{
            { "partId", part.partId },
            { "displayName", part.displayName },
            { "defaultModelAssetId", part.defaultModelAssetId },
            { "visibleByDefault", part.visibleByDefault }
        };

        if (!part.slotId.empty())
        {
            result["slotId"] = part.slotId;
        }

        if (!part.bindingStates.empty())
        {
            result["binding"] = {
                { "defaultStateId", part.defaultStateId },
                { "states", std::move(states) }
            };
        }

        return result;
    }

    VoxelAssemblyPartDefinition part_definition_from_json(const nlohmann::json& node)
    {
        VoxelAssemblyPartDefinition part{};
        part.partId = node.value("partId", part.partId);
        part.displayName = node.value("displayName", part.displayName);
        part.defaultModelAssetId = node.value("defaultModelAssetId", part.defaultModelAssetId);
        part.visibleByDefault = node.value("visibleByDefault", part.visibleByDefault);
        part.slotId = node.value("slotId", part.slotId);

        if (node.contains("binding") && node.at("binding").is_object())
        {
            const auto& bindingNode = node.at("binding");
            part.defaultStateId = bindingNode.value("defaultStateId", part.defaultStateId);
            if (bindingNode.contains("states") && bindingNode.at("states").is_array())
            {
                for (const auto& stateNode : bindingNode.at("states"))
                {
                    if (!stateNode.is_object())
                    {
                        continue;
                    }

                    VoxelAssemblyBindingState state = binding_state_from_json(stateNode);
                    if (state.stateId.empty())
                    {
                        continue;
                    }
                    part.bindingStates.push_back(std::move(state));
                }
            }
        }

        return part;
    }

    nlohmann::json slot_definition_to_json(const VoxelAssemblySlotDefinition& slot)
    {
        return {
            { "slotId", slot.slotId },
            { "displayName", slot.displayName },
            { "fallbackPartId", slot.fallbackPartId },
            { "required", slot.required }
        };
    }

    VoxelAssemblySlotDefinition slot_definition_from_json(const nlohmann::json& node)
    {
        VoxelAssemblySlotDefinition slot{};
        slot.slotId = node.value("slotId", slot.slotId);
        slot.displayName = node.value("displayName", slot.displayName);
        slot.fallbackPartId = node.value("fallbackPartId", slot.fallbackPartId);
        slot.required = node.value("required", slot.required);
        return slot;
    }

    nlohmann::json serialize(const VoxelAssemblyAsset& asset)
    {
        nlohmann::json parts = nlohmann::json::array();
        for (const VoxelAssemblyPartDefinition& part : asset.parts)
        {
            parts.push_back(part_definition_to_json(part));
        }

        nlohmann::json slots = nlohmann::json::array();
        for (const VoxelAssemblySlotDefinition& slot : asset.slots)
        {
            slots.push_back(slot_definition_to_json(slot));
        }

        return {
            { "version", VoxelAssemblyVersion },
            { "assetId", asset.assetId },
            { "displayName", asset.displayName },
            { "rootPartId", asset.rootPartId },
            { "parts", std::move(parts) },
            { "slots", std::move(slots) }
        };
    }

    VoxelAssemblyAsset deserialize(const nlohmann::json& document)
    {
        VoxelAssemblyAsset asset{};
        asset.assetId = sanitize_asset_id(document.value("assetId", asset.assetId));
        asset.displayName = document.value("displayName", asset.displayName);
        asset.rootPartId = document.value("rootPartId", asset.rootPartId);

        if (document.contains("parts") && document.at("parts").is_array())
        {
            for (const auto& partNode : document.at("parts"))
            {
                if (!partNode.is_object())
                {
                    continue;
                }

                VoxelAssemblyPartDefinition part = part_definition_from_json(partNode);
                if (part.partId.empty() || part.defaultModelAssetId.empty())
                {
                    continue;
                }
                asset.parts.push_back(std::move(part));
            }
        }

        if (document.contains("slots") && document.at("slots").is_array())
        {
            for (const auto& slotNode : document.at("slots"))
            {
                if (!slotNode.is_object())
                {
                    continue;
                }

                VoxelAssemblySlotDefinition slot = slot_definition_from_json(slotNode);
                if (slot.slotId.empty())
                {
                    continue;
                }
                asset.slots.push_back(std::move(slot));
            }
        }

        return asset;
    }
}

VoxelAssemblyRepository::VoxelAssemblyRepository(
    const config::IJsonDocumentStore& documentStore,
    std::filesystem::path rootPath) :
    _documentStore(documentStore),
    _rootPath(std::move(rootPath))
{
}

std::optional<VoxelAssemblyAsset> VoxelAssemblyRepository::load(const std::string_view assetId) const
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

std::vector<std::string> VoxelAssemblyRepository::list_asset_ids() const
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
            if (!filename.ends_with(VoxelAssemblyFileSuffix))
            {
                continue;
            }

            const std::string_view filenameView{filename};
            const std::string_view assetIdView = filenameView.substr(
                0,
                filenameView.size() - VoxelAssemblyFileSuffix.size());
            assetIds.push_back(std::string(assetIdView));
        }

        std::ranges::sort(assetIds);
    }
    catch (const std::exception&)
    {
    }

    return assetIds;
}

void VoxelAssemblyRepository::save(const VoxelAssemblyAsset& asset) const
{
    if (asset.rootPartId.empty())
    {
        throw std::runtime_error("VoxelAssemblyRepository::save: rootPartId must not be empty");
    }

    VoxelAssemblyAsset normalized = asset;
    normalized.assetId = sanitize_asset_id(asset.assetId);
    if (normalized.displayName.empty())
    {
        normalized.displayName = normalized.assetId;
    }

    _documentStore.save(resolve_path(normalized.assetId), serialize(normalized));
}

std::filesystem::path VoxelAssemblyRepository::resolve_path(const std::string_view assetId) const
{
    return _rootPath / std::format("{}.vxma.json", sanitize_asset_id(assetId));
}

const std::filesystem::path& VoxelAssemblyRepository::root_path() const noexcept
{
    return _rootPath;
}
