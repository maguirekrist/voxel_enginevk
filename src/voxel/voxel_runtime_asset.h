#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "render/mesh.h"
#include "voxel_model.h"

struct VoxelRuntimeAsset
{
    std::string assetId{};
    VoxelModel model{};
    std::shared_ptr<Mesh> mesh{};
    VoxelBounds bounds{};
    std::unordered_map<std::string, VoxelAttachment> attachments{};
};
