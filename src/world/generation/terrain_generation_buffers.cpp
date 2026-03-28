#include "../terrain_gen.h"

#include <cmath>
#include <limits>
#include <ranges>

namespace
{
    [[nodiscard]] size_t grid2d_index(const int size, const int x, const int z)
    {
        return static_cast<size_t>((z * size) + x);
    }

    [[nodiscard]] size_t grid3d_index(const int sizeX, const int sizeY, const int x, const int y, const int z)
    {
        return static_cast<size_t>(((z * sizeY) + y) * sizeX + x);
    }
}

uint32_t pack_appearance_color(const glm::vec3& color) noexcept
{
    const glm::vec3 clamped = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    return pack_appearance_color(glm::u8vec3{
        static_cast<uint8_t>(std::round(clamped.r * 255.0f)),
        static_cast<uint8_t>(std::round(clamped.g * 255.0f)),
        static_cast<uint8_t>(std::round(clamped.b * 255.0f))
    });
}

glm::vec3 unpack_appearance_color(const uint32_t packedColor) noexcept
{
    const float r = static_cast<float>(packedColor & 0xFFu) / 255.0f;
    const float g = static_cast<float>((packedColor >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>((packedColor >> 16) & 0xFFu) / 255.0f;
    return glm::vec3(r, g, b);
}

const WorldRegionSample& WorldRegionScaffold2D::at(const int localX, const int localZ) const
{
    return cells[grid2d_index(CHUNK_SIZE, localX, localZ)];
}

WorldRegionSample& WorldRegionScaffold2D::at(const int localX, const int localZ)
{
    return cells[grid2d_index(CHUNK_SIZE, localX, localZ)];
}

const TerrainColumnSample& TerrainColumnScaffold2D::at(const int localX, const int localZ) const
{
    return columns[grid2d_index(CHUNK_SIZE, localX, localZ)];
}

TerrainColumnSample& TerrainColumnScaffold2D::at(const int localX, const int localZ)
{
    return columns[grid2d_index(CHUNK_SIZE, localX, localZ)];
}

const TerrainFeatureInstance* TerrainFeatureInstanceSet::find_by_id(const uint32_t id) const
{
    if (id == 0u || features.empty())
    {
        return nullptr;
    }

    const auto it = std::ranges::find_if(features, [id](const TerrainFeatureInstance& feature)
    {
        return feature.id == id;
    });
    return it != features.end() ? &(*it) : nullptr;
}

const TerrainVolumeCell& TerrainVolumeBuffer::at(const int localX, const int y, const int localZ) const
{
    return cells[grid3d_index(CHUNK_SIZE, CHUNK_HEIGHT, localX, y, localZ)];
}

TerrainVolumeCell& TerrainVolumeBuffer::at(const int localX, const int y, const int localZ)
{
    return cells[grid3d_index(CHUNK_SIZE, CHUNK_HEIGHT, localX, y, localZ)];
}

const std::array<SurfaceClass, 6>& SurfaceClassificationBuffer::at(const int localX, const int y, const int localZ) const
{
    return faces[grid3d_index(CHUNK_SIZE, CHUNK_HEIGHT, localX, y, localZ)];
}

std::array<SurfaceClass, 6>& SurfaceClassificationBuffer::at(const int localX, const int y, const int localZ)
{
    return faces[grid3d_index(CHUNK_SIZE, CHUNK_HEIGHT, localX, y, localZ)];
}

const TerrainAppearanceVoxel& AppearanceBuffer::at(const int localX, const int y, const int localZ) const
{
    return voxels[grid3d_index(CHUNK_SIZE, CHUNK_HEIGHT, localX, y, localZ)];
}

TerrainAppearanceVoxel& AppearanceBuffer::at(const int localX, const int y, const int localZ)
{
    return voxels[grid3d_index(CHUNK_SIZE, CHUNK_HEIGHT, localX, y, localZ)];
}

uint32_t AppearanceBuffer::packed_color(const int localX, const int y, const int localZ) const
{
    return at(localX, y, localZ).color;
}
