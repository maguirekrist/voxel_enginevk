#include "decoration.h"

#include <algorithm>

#include <glm/ext/quaternion_trigonometric.hpp>

#include "chunk.h"
#include "random.h"
#include "world/terrain_gen.h"

namespace
{
    constexpr int FlowerPlacementCellSize = 4;
    constexpr int ForestFlowerChancePercent = 26;
    constexpr int FlowerClearanceHeight = 2;
    constexpr std::string_view FlowerAssetId = "flower";

    int floor_divide(const int value, const int divisor)
    {
        int quotient = value / divisor;
        const int remainder = value % divisor;
        if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
        {
            --quotient;
        }

        return quotient;
    }

    class ForestFlowerPlacementStrategy final : public IWorldDecorationPlacementStrategy
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "ForestFlowers";
        }

        void collect_placements(const DecorationGenerationContext& context, std::vector<VoxelDecorationPlacement>& placements) const override
        {
            if (context.terrainGenerator == nullptr || context.chunkData == nullptr)
            {
                return;
            }

            const int minCellX = floor_divide(context.chunkOrigin.x, FlowerPlacementCellSize);
            const int maxCellX = floor_divide(context.chunkOrigin.x + static_cast<int>(CHUNK_SIZE) - 1, FlowerPlacementCellSize);
            const int minCellZ = floor_divide(context.chunkOrigin.y, FlowerPlacementCellSize);
            const int maxCellZ = floor_divide(context.chunkOrigin.y + static_cast<int>(CHUNK_SIZE) - 1, FlowerPlacementCellSize);

            placements.reserve(placements.size() + static_cast<size_t>((maxCellX - minCellX + 1) * (maxCellZ - minCellZ + 1)));

            for (int cellX = minCellX; cellX <= maxCellX; ++cellX)
            {
                for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
                {
                    uint64_t cellSeed = Random::seed_from_ints({
                        context.chunkOrigin.x,
                        context.chunkOrigin.y,
                        cellX,
                        cellZ,
                        FlowerPlacementCellSize,
                        0xF10A
                    });
                    const int worldX = cellX * FlowerPlacementCellSize + Random::generate_from_seed(cellSeed, 0, FlowerPlacementCellSize - 1);
                    const int worldZ = cellZ * FlowerPlacementCellSize + Random::generate_from_seed(cellSeed, 0, FlowerPlacementCellSize - 1);

                    if (worldX < context.chunkOrigin.x ||
                        worldX >= context.chunkOrigin.x + static_cast<int>(CHUNK_SIZE) ||
                        worldZ < context.chunkOrigin.y ||
                        worldZ >= context.chunkOrigin.y + static_cast<int>(CHUNK_SIZE))
                    {
                        continue;
                    }

                    const TerrainColumnSample column = context.terrainGenerator->SampleColumn(worldX, worldZ);
                    if (!decoration::is_forest_flower_biome(column.biome))
                    {
                        continue;
                    }

                    if (Random::generate_from_seed(cellSeed, 0, 99) >= ForestFlowerChancePercent)
                    {
                        continue;
                    }

                    const glm::ivec3 baseWorldPos{ worldX, column.surfaceHeight, worldZ };
                    if (!decoration::can_place_surface_decoration(*context.chunkData, column, baseWorldPos, FlowerClearanceHeight))
                    {
                        continue;
                    }

                    const float yawDegrees = static_cast<float>(Random::generate_from_seed(cellSeed, 0, 359));
                    const float scale = static_cast<float>(Random::generate_from_seed(cellSeed, 90, 120)) / 100.0f;

                    placements.push_back(VoxelDecorationPlacement{
                        .assetId = std::string(FlowerAssetId),
                        .worldPosition = glm::vec3(
                            static_cast<float>(worldX) + 0.5f,
                            static_cast<float>(column.surfaceHeight + 1),
                            static_cast<float>(worldZ) + 0.5f),
                        .rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)),
                        .scale = scale,
                        .placementPolicy = VoxelPlacementPolicy::BottomCenter
                    });
                }
            }
        }
    };
}

DecorationRegistry& DecorationRegistry::instance()
{
    static DecorationRegistry instance;
    return instance;
}

std::vector<VoxelDecorationPlacement> DecorationRegistry::generate_for_chunk(const DecorationGenerationContext& context) const
{
    std::vector<VoxelDecorationPlacement> placements{};
    for (const auto& strategy : _placementStrategies)
    {
        if (strategy == nullptr)
        {
            continue;
        }

        strategy->collect_placements(context, placements);
    }

    return placements;
}

DecorationRegistry::DecorationRegistry()
{
    _placementStrategies.push_back(std::make_unique<ForestFlowerPlacementStrategy>());
}

bool decoration::is_forest_flower_biome(const BiomeType biome) noexcept
{
    return biome == BiomeType::Forest;
}

bool decoration::has_vertical_clearance(const ChunkData& chunkData, const glm::ivec3& baseWorldPos, const int clearanceHeight)
{
    for (int clearance = 1; clearance <= clearanceHeight; ++clearance)
    {
        const glm::ivec3 checkWorldPos = baseWorldPos + glm::ivec3(0, clearance, 0);
        if (!chunkData.contains_world_position(checkWorldPos))
        {
            return false;
        }

        const glm::ivec3 localCheck = chunkData.to_local_position(checkWorldPos);
        const Block& aboveBlock = chunkData.blocks[localCheck.x][localCheck.y][localCheck.z];
        if (aboveBlock._solid || aboveBlock._type != BlockType::AIR)
        {
            return false;
        }
    }

    return true;
}

bool decoration::can_place_surface_decoration(
    const ChunkData& chunkData,
    const TerrainColumnSample& column,
    const glm::ivec3& baseWorldPos,
    const int clearanceHeight)
{
    if (!chunkData.contains_world_position(baseWorldPos))
    {
        return false;
    }

    const glm::ivec3 localBase = chunkData.to_local_position(baseWorldPos);
    const Block& groundBlock = chunkData.blocks[localBase.x][localBase.y][localBase.z];
    if (groundBlock._type != BlockType::GROUND || !groundBlock._solid)
    {
        return false;
    }

    return has_vertical_clearance(chunkData, baseWorldPos, clearanceHeight) &&
        column.biome != BiomeType::Ocean &&
        !column.isBeach;
}
