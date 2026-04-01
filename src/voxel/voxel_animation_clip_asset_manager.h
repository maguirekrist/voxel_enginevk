#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "voxel_animation_asset.h"
#include "voxel_animation_clip_repository.h"

class VoxelAnimationClipAssetManager
{
public:
    explicit VoxelAnimationClipAssetManager(const VoxelAnimationClipRepository& repository);

    [[nodiscard]] std::shared_ptr<const VoxelAnimationClipAsset> load_or_get(std::string_view assetId);
    [[nodiscard]] std::shared_ptr<const VoxelAnimationClipAsset> find_loaded(std::string_view assetId) const;
    [[nodiscard]] size_t loaded_asset_count() const noexcept;
    void clear();

private:
    [[nodiscard]] static std::string normalize_asset_id(std::string_view assetId);

    const VoxelAnimationClipRepository& _repository;
    std::unordered_map<std::string, std::shared_ptr<const VoxelAnimationClipAsset>> _loadedAssets{};
};
