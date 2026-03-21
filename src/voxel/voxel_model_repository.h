#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/json_document_store.h"
#include "voxel_model.h"

class VoxelModelRepository
{
public:
    explicit VoxelModelRepository(
        const config::IJsonDocumentStore& documentStore,
        std::filesystem::path rootPath = std::filesystem::path("models") / "voxels");

    [[nodiscard]] std::optional<VoxelModel> load(std::string_view assetId) const;
    [[nodiscard]] std::vector<std::string> list_asset_ids() const;
    void save(const VoxelModel& model) const;
    [[nodiscard]] std::filesystem::path resolve_path(std::string_view assetId) const;
    [[nodiscard]] const std::filesystem::path& root_path() const noexcept;

private:
    const config::IJsonDocumentStore& _documentStore;
    std::filesystem::path _rootPath{};
};
