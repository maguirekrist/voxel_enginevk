#pragma once

#include <memory>

#include "render/mesh.h"
#include "voxel_model.h"

class VoxelMesher
{
public:
    [[nodiscard]] static std::shared_ptr<Mesh> generate_mesh(const VoxelModel& model);
};
