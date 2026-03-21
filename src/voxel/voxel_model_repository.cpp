#include "voxel_model_repository.h"

#include <cctype>
#include <algorithm>
#include <format>
#include <stdexcept>

namespace
{
    constexpr int VoxelModelVersion = 1;
    constexpr std::string_view VoxelModelFileSuffix = ".vxm.json";

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

    nlohmann::json color_to_json(const VoxelColor& color)
    {
        return {
            { "r", color.r },
            { "g", color.g },
            { "b", color.b },
            { "a", color.a }
        };
    }

    VoxelColor color_from_json(const nlohmann::json& node)
    {
        return VoxelColor{
            .r = node.value("r", static_cast<uint8_t>(255)),
            .g = node.value("g", static_cast<uint8_t>(255)),
            .b = node.value("b", static_cast<uint8_t>(255)),
            .a = node.value("a", static_cast<uint8_t>(255))
        };
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

    nlohmann::json serialize(const VoxelModel& model)
    {
        nlohmann::json voxels = nlohmann::json::array();

        for (const auto& [coord, color] : model.voxels())
        {
            voxels.push_back({
                { "x", coord.x },
                { "y", coord.y },
                { "z", coord.z },
                { "color", color_to_json(color) }
            });
        }

        return {
            { "version", VoxelModelVersion },
            { "assetId", model.assetId },
            { "displayName", model.displayName },
            { "voxelSize", model.voxelSize },
            { "pivot", vec3_to_json(model.pivot) },
            { "voxels", std::move(voxels) }
        };
    }

    VoxelModel deserialize(const nlohmann::json& document)
    {
        VoxelModel model{};
        model.assetId = sanitize_asset_id(document.value("assetId", model.assetId));
        model.displayName = document.value("displayName", model.displayName);
        model.voxelSize = document.value("voxelSize", model.voxelSize);

        if (document.contains("pivot") && document.at("pivot").is_object())
        {
            model.pivot = vec3_from_json(document.at("pivot"), model.pivot);
        }

        if (document.contains("voxels") && document.at("voxels").is_array())
        {
            for (const auto& voxelNode : document.at("voxels"))
            {
                if (!voxelNode.is_object())
                {
                    continue;
                }

                const VoxelCoord coord{
                    .x = voxelNode.value("x", 0),
                    .y = voxelNode.value("y", 0),
                    .z = voxelNode.value("z", 0)
                };
                const VoxelColor color = voxelNode.contains("color") && voxelNode.at("color").is_object()
                    ? color_from_json(voxelNode.at("color"))
                    : VoxelColor{};
                model.set_voxel(coord, color);
            }
        }

        return model;
    }
}

VoxelModelRepository::VoxelModelRepository(
    const config::IJsonDocumentStore& documentStore,
    std::filesystem::path rootPath) :
    _documentStore(documentStore),
    _rootPath(std::move(rootPath))
{
}

std::optional<VoxelModel> VoxelModelRepository::load(const std::string_view assetId) const
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

std::vector<std::string> VoxelModelRepository::list_asset_ids() const
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
            if (!filename.ends_with(VoxelModelFileSuffix))
            {
                continue;
            }

            const std::string_view filenameView{filename};
            const std::string_view assetIdView = filenameView.substr(0, filenameView.size() - VoxelModelFileSuffix.size());
            assetIds.push_back(std::string(assetIdView));
        }

        std::ranges::sort(assetIds);
    }
    catch (const std::exception&)
    {
    }

    return assetIds;
}

void VoxelModelRepository::save(const VoxelModel& model) const
{
    if (model.voxelSize <= 0.0f)
    {
        throw std::runtime_error("VoxelModelRepository::save: voxelSize must be positive");
    }

    VoxelModel normalized = model;
    normalized.assetId = sanitize_asset_id(model.assetId);
    if (normalized.displayName.empty())
    {
        normalized.displayName = normalized.assetId;
    }

    _documentStore.save(resolve_path(normalized.assetId), serialize(normalized));
}

std::filesystem::path VoxelModelRepository::resolve_path(const std::string_view assetId) const
{
    return _rootPath / std::format("{}.vxm.json", sanitize_asset_id(assetId));
}

const std::filesystem::path& VoxelModelRepository::root_path() const noexcept
{
    return _rootPath;
}
