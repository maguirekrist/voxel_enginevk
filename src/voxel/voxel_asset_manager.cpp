#include "voxel_asset_manager.h"

#include <cctype>

#include "voxel_mesher.h"

namespace
{
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
}

VoxelAssetManager::VoxelAssetManager(const VoxelModelRepository& repository) :
    _repository(repository)
{
}

std::shared_ptr<VoxelRuntimeAsset> VoxelAssetManager::load_or_get(const std::string_view assetId)
{
    const std::string normalizedId = normalize_asset_id(assetId);
    if (const auto it = _loadedAssets.find(normalizedId); it != _loadedAssets.end())
    {
        return it->second;
    }

    const std::optional<VoxelModel> loaded = _repository.load(normalizedId);
    if (!loaded.has_value())
    {
        return nullptr;
    }

    std::shared_ptr<VoxelRuntimeAsset> runtimeAsset = build_runtime_asset(loaded.value());
    _loadedAssets.insert_or_assign(normalizedId, runtimeAsset);
    return runtimeAsset;
}

std::shared_ptr<const VoxelRuntimeAsset> VoxelAssetManager::find_loaded(const std::string_view assetId) const
{
    const std::string normalizedId = normalize_asset_id(assetId);
    if (const auto it = _loadedAssets.find(normalizedId); it != _loadedAssets.end())
    {
        return it->second;
    }

    return nullptr;
}

size_t VoxelAssetManager::loaded_asset_count() const noexcept
{
    return _loadedAssets.size();
}

void VoxelAssetManager::clear()
{
    _loadedAssets.clear();
}

std::string VoxelAssetManager::normalize_asset_id(const std::string_view assetId)
{
    return sanitize_asset_id(assetId);
}

std::shared_ptr<VoxelRuntimeAsset> VoxelAssetManager::build_runtime_asset(const VoxelModel& model) const
{
    auto runtimeAsset = std::make_shared<VoxelRuntimeAsset>();
    runtimeAsset->assetId = normalize_asset_id(model.assetId);
    runtimeAsset->model = model;
    runtimeAsset->mesh = VoxelMesher::generate_mesh(runtimeAsset->model);
    runtimeAsset->bounds = runtimeAsset->model.bounds();

    for (const VoxelAttachment& attachment : runtimeAsset->model.attachments)
    {
        runtimeAsset->attachments.insert_or_assign(attachment.name, attachment);
    }

    return runtimeAsset;
}
