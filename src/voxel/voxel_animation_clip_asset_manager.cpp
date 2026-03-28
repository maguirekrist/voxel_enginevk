#include "voxel_animation_clip_asset_manager.h"

#include <cctype>

namespace
{
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
}

VoxelAnimationClipAssetManager::VoxelAnimationClipAssetManager(const VoxelAnimationClipRepository& repository) :
    _repository(repository)
{
}

std::shared_ptr<const VoxelAnimationClipAsset> VoxelAnimationClipAssetManager::load_or_get(const std::string_view assetId)
{
    const std::string normalizedId = normalize_asset_id(assetId);
    if (const auto it = _loadedAssets.find(normalizedId); it != _loadedAssets.end())
    {
        return it->second;
    }

    const std::optional<VoxelAnimationClipAsset> loaded = _repository.load(normalizedId);
    if (!loaded.has_value())
    {
        return nullptr;
    }

    std::shared_ptr<const VoxelAnimationClipAsset> cachedAsset = std::make_shared<VoxelAnimationClipAsset>(loaded.value());
    _loadedAssets.insert_or_assign(normalizedId, cachedAsset);
    return cachedAsset;
}

std::shared_ptr<const VoxelAnimationClipAsset> VoxelAnimationClipAssetManager::find_loaded(const std::string_view assetId) const
{
    const std::string normalizedId = normalize_asset_id(assetId);
    if (const auto it = _loadedAssets.find(normalizedId); it != _loadedAssets.end())
    {
        return it->second;
    }

    return nullptr;
}

size_t VoxelAnimationClipAssetManager::loaded_asset_count() const noexcept
{
    return _loadedAssets.size();
}

void VoxelAnimationClipAssetManager::clear()
{
    _loadedAssets.clear();
}

std::string VoxelAnimationClipAssetManager::normalize_asset_id(const std::string_view assetId)
{
    return sanitize_asset_id(assetId);
}
