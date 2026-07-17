#pragma once

#include <glm/vec3.hpp>

#include "constants.h"
#include "game/chunk.h"

struct WorldGeometrySettings
{
    int chunkVoxelWidth = static_cast<int>(CHUNK_SIZE);
    int chunkVoxelHeight = static_cast<int>(CHUNK_HEIGHT);
    float chunkWorldWidth = static_cast<float>(CHUNK_SIZE);
    float chunkWorldHeight = static_cast<float>(CHUNK_HEIGHT);
};

[[nodiscard]] WorldGeometrySettings default_world_geometry_settings() noexcept;
void normalize_world_geometry_settings(WorldGeometrySettings& settings) noexcept;

class WorldGeometry
{
public:
    WorldGeometry() noexcept;
    explicit WorldGeometry(WorldGeometrySettings settings) noexcept;

    [[nodiscard]] const WorldGeometrySettings& settings() const noexcept;

    [[nodiscard]] int chunk_voxel_width() const noexcept;
    [[nodiscard]] int chunk_voxel_height() const noexcept;
    [[nodiscard]] int chunk_voxel_depth() const noexcept;

    [[nodiscard]] float chunk_world_width() const noexcept;
    [[nodiscard]] float chunk_world_height() const noexcept;
    [[nodiscard]] float chunk_world_depth() const noexcept;
    [[nodiscard]] float block_world_size() const noexcept;

    [[nodiscard]] glm::vec3 voxel_to_world(const glm::vec3& voxel) const noexcept;
    [[nodiscard]] glm::vec3 world_to_voxel(const glm::vec3& world) const noexcept;
    [[nodiscard]] glm::ivec3 world_to_voxel_cell(const glm::vec3& world) const noexcept;
    [[nodiscard]] ChunkCoord world_to_chunk(const glm::vec3& world) const noexcept;
    [[nodiscard]] glm::ivec3 world_to_local_voxel(const glm::vec3& world) const noexcept;
    [[nodiscard]] glm::ivec3 chunk_voxel_origin(const ChunkCoord& coord) const noexcept;
    [[nodiscard]] glm::vec3 chunk_world_origin(const ChunkCoord& coord) const noexcept;

private:
    WorldGeometrySettings _settings{};
    float _blockWorldSize = 1.0f;
};
