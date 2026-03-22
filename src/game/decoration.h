#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "world/terrain_gen.h"

class TerrainGenerator;
struct ChunkData;

struct VoxelDecorationPlacement
{
    std::string assetId{};
    glm::vec3 worldPosition{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float scale{1.0f};
};

struct DecorationGenerationContext
{
    glm::ivec2 chunkOrigin{};
    const TerrainGenerator* terrainGenerator{nullptr};
    const ChunkData* chunkData{nullptr};
};

class IWorldDecorationPlacementStrategy
{
public:
    virtual ~IWorldDecorationPlacementStrategy() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual void collect_placements(const DecorationGenerationContext& context, std::vector<VoxelDecorationPlacement>& placements) const = 0;
};

class DecorationRegistry
{
public:
    static DecorationRegistry& instance();

    [[nodiscard]] std::vector<VoxelDecorationPlacement> generate_for_chunk(const DecorationGenerationContext& context) const;

private:
    DecorationRegistry();

    std::vector<std::unique_ptr<IWorldDecorationPlacementStrategy>> _placementStrategies{};
};

namespace decoration
{
    [[nodiscard]] bool is_forest_flower_biome(BiomeType biome) noexcept;
    [[nodiscard]] bool has_vertical_clearance(const ChunkData& chunkData, const glm::ivec3& baseWorldPos, int clearanceHeight);
    [[nodiscard]] bool can_place_surface_decoration(
        const ChunkData& chunkData,
        const TerrainColumnSample& column,
        const glm::ivec3& baseWorldPos,
        int clearanceHeight);
}
