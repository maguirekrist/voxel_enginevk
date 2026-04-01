#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/json_document_store.h"
#include "voxel_animation_asset.h"

class VoxelAnimationClipRepository
{
public:
    explicit VoxelAnimationClipRepository(
        const config::IJsonDocumentStore& documentStore,
        std::filesystem::path rootPath = std::filesystem::path("models") / "voxel_animations");

    [[nodiscard]] std::optional<VoxelAnimationClipAsset> load(std::string_view assetId) const;
    [[nodiscard]] std::vector<std::string> list_asset_ids() const;
    void save(const VoxelAnimationClipAsset& asset) const;
    bool remove(std::string_view assetId) const;
    [[nodiscard]] std::filesystem::path resolve_path(std::string_view assetId) const;
    [[nodiscard]] const std::filesystem::path& root_path() const noexcept;

private:
    const config::IJsonDocumentStore& _documentStore;
    std::filesystem::path _rootPath{};
};
