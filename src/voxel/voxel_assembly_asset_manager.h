#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "voxel_assembly_asset.h"
#include "voxel_assembly_repository.h"

class VoxelAssemblyAssetManager
{
public:
    explicit VoxelAssemblyAssetManager(const VoxelAssemblyRepository& repository);

    [[nodiscard]] std::shared_ptr<const VoxelAssemblyAsset> load_or_get(std::string_view assetId);
    [[nodiscard]] std::shared_ptr<const VoxelAssemblyAsset> find_loaded(std::string_view assetId) const;
    [[nodiscard]] size_t loaded_asset_count() const noexcept;
    void clear();

private:
    [[nodiscard]] static std::string normalize_asset_id(std::string_view assetId);

    const VoxelAssemblyRepository& _repository;
    std::unordered_map<std::string, std::shared_ptr<const VoxelAssemblyAsset>> _loadedAssets{};
};
