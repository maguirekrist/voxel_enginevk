#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "voxel_animation_asset.h"
#include "voxel_animation_controller_repository.h"

class VoxelAnimationControllerAssetManager
{
public:
    explicit VoxelAnimationControllerAssetManager(const VoxelAnimationControllerRepository& repository);

    [[nodiscard]] std::shared_ptr<const VoxelAnimationControllerAsset> load_or_get(std::string_view assetId);
    [[nodiscard]] std::shared_ptr<const VoxelAnimationControllerAsset> find_loaded(std::string_view assetId) const;
    [[nodiscard]] size_t loaded_asset_count() const noexcept;
    void clear();

private:
    [[nodiscard]] static std::string normalize_asset_id(std::string_view assetId);

    const VoxelAnimationControllerRepository& _repository;
    std::unordered_map<std::string, std::shared_ptr<const VoxelAnimationControllerAsset>> _loadedAssets{};
};
