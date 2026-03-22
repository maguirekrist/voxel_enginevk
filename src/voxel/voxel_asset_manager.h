#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "voxel_model_repository.h"
#include "voxel_runtime_asset.h"

class VoxelAssetManager
{
public:
    explicit VoxelAssetManager(const VoxelModelRepository& repository);

    [[nodiscard]] std::shared_ptr<VoxelRuntimeAsset> load_or_get(std::string_view assetId);
    [[nodiscard]] std::shared_ptr<const VoxelRuntimeAsset> find_loaded(std::string_view assetId) const;
    [[nodiscard]] size_t loaded_asset_count() const noexcept;
    void clear();

private:
    [[nodiscard]] static std::string normalize_asset_id(std::string_view assetId);
    [[nodiscard]] std::shared_ptr<VoxelRuntimeAsset> build_runtime_asset(const VoxelModel& model) const;

    const VoxelModelRepository& _repository;
    std::unordered_map<std::string, std::shared_ptr<VoxelRuntimeAsset>> _loadedAssets{};
};
