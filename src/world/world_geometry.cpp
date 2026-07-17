#include "world_geometry.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float MinimumWorldDimension = 0.001f;

    [[nodiscard]] int floor_to_int(const float value) noexcept
    {
        return static_cast<int>(std::floor(value));
    }

    [[nodiscard]] int wrap_to_chunk_axis(const int value, const int axisSize) noexcept
    {
        const int mod = value % axisSize;
        return mod < 0 ? mod + axisSize : mod;
    }
}

WorldGeometrySettings default_world_geometry_settings() noexcept
{
    return {};
}

void normalize_world_geometry_settings(WorldGeometrySettings& settings) noexcept
{
    settings.chunkVoxelWidth = std::max(1, settings.chunkVoxelWidth);
    settings.chunkVoxelHeight = std::max(1, settings.chunkVoxelHeight);
    settings.chunkWorldWidth = std::max(MinimumWorldDimension, settings.chunkWorldWidth);
    settings.chunkWorldHeight = std::max(MinimumWorldDimension, settings.chunkWorldHeight);

    const float blockWorldSize = settings.chunkWorldWidth / static_cast<float>(settings.chunkVoxelWidth);
    settings.chunkVoxelHeight = std::max(
        1,
        static_cast<int>(std::lround(settings.chunkWorldHeight / blockWorldSize)));
}

WorldGeometry::WorldGeometry() noexcept :
    WorldGeometry(default_world_geometry_settings())
{
}

WorldGeometry::WorldGeometry(WorldGeometrySettings settings) noexcept :
    _settings(std::move(settings))
{
    normalize_world_geometry_settings(_settings);
    _blockWorldSize = _settings.chunkWorldWidth / static_cast<float>(_settings.chunkVoxelWidth);
}

const WorldGeometrySettings& WorldGeometry::settings() const noexcept
{
    return _settings;
}

int WorldGeometry::chunk_voxel_width() const noexcept
{
    return _settings.chunkVoxelWidth;
}

int WorldGeometry::chunk_voxel_height() const noexcept
{
    return _settings.chunkVoxelHeight;
}

int WorldGeometry::chunk_voxel_depth() const noexcept
{
    return _settings.chunkVoxelWidth;
}

float WorldGeometry::chunk_world_width() const noexcept
{
    return _settings.chunkWorldWidth;
}

float WorldGeometry::chunk_world_height() const noexcept
{
    return _settings.chunkWorldHeight;
}

float WorldGeometry::chunk_world_depth() const noexcept
{
    return _settings.chunkWorldWidth;
}

float WorldGeometry::block_world_size() const noexcept
{
    return _blockWorldSize;
}

glm::vec3 WorldGeometry::voxel_to_world(const glm::vec3& voxel) const noexcept
{
    return voxel * _blockWorldSize;
}

glm::vec3 WorldGeometry::world_to_voxel(const glm::vec3& world) const noexcept
{
    return world / _blockWorldSize;
}

glm::ivec3 WorldGeometry::world_to_voxel_cell(const glm::vec3& world) const noexcept
{
    const glm::vec3 voxel = world_to_voxel(world);
    return {
        floor_to_int(voxel.x),
        floor_to_int(voxel.y),
        floor_to_int(voxel.z)
    };
}

ChunkCoord WorldGeometry::world_to_chunk(const glm::vec3& world) const noexcept
{
    return {
        floor_to_int(world.x / _settings.chunkWorldWidth),
        floor_to_int(world.z / _settings.chunkWorldWidth)
    };
}

glm::ivec3 WorldGeometry::world_to_local_voxel(const glm::vec3& world) const noexcept
{
    const glm::ivec3 voxel = world_to_voxel_cell(world);
    return {
        wrap_to_chunk_axis(voxel.x, _settings.chunkVoxelWidth),
        voxel.y,
        wrap_to_chunk_axis(voxel.z, _settings.chunkVoxelWidth)
    };
}

glm::ivec3 WorldGeometry::chunk_voxel_origin(const ChunkCoord& coord) const noexcept
{
    return {
        coord.x * _settings.chunkVoxelWidth,
        0,
        coord.z * _settings.chunkVoxelWidth
    };
}

glm::vec3 WorldGeometry::chunk_world_origin(const ChunkCoord& coord) const noexcept
{
    return {
        static_cast<float>(coord.x) * _settings.chunkWorldWidth,
        0.0f,
        static_cast<float>(coord.z) * _settings.chunkWorldWidth
    };
}
