#include "world/structures/cloud_structure_generator.h"

#include <algorithm>
#include <cmath>

#include "constants.h"
#include "random.h"

namespace
{
    constexpr float CloudPlacementCellSizeWorld = 40.0f;
    constexpr int CloudSpawnChancePercent = 42;
    constexpr float CloudMinBaseHeightWorld = 148.0f;
    constexpr float CloudMaxBaseHeightWorld = 196.0f;
    constexpr float CloudMaxRadiusWorld = 11.0f;
    constexpr float CloudMaxHeightWorld = 8.0f;

    [[nodiscard]] int world_units_to_voxels_round(const float worldUnits, const float blockWorldSize)
    {
        return std::max(1, static_cast<int>(std::lround(worldUnits / blockWorldSize)));
    }

    [[nodiscard]] int world_units_to_voxels_ceil(const float worldUnits, const float blockWorldSize)
    {
        return std::max(1, static_cast<int>(std::ceil(worldUnits / blockWorldSize)));
    }

    [[nodiscard]] int world_offset_to_voxels_round(const float worldUnits, const float blockWorldSize)
    {
        return static_cast<int>(std::lround(worldUnits / blockWorldSize));
    }

    [[nodiscard]] Block make_cloud_block()
    {
        return Block{
            ._solid = true,
            ._sunlight = 0,
            ._type = static_cast<uint8_t>(BlockType::CLOUD)
        };
    }

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

    void add_cloud_puff(
        std::vector<StructureBlockEdit>& edits,
        const glm::ivec3 center,
        const int radiusXZ,
        const int radiusY,
        const int baseY,
        const uint64_t seed)
    {
        const int horizontalExtent = std::max(1, radiusXZ + 1);
        const int verticalExtent = std::max(1, radiusY);

        for (int y = 0; y <= verticalExtent; ++y)
        {
            for (int x = -horizontalExtent; x <= horizontalExtent; ++x)
            {
                for (int z = -horizontalExtent; z <= horizontalExtent; ++z)
                {
                    const float normalizedX = static_cast<float>(x) / static_cast<float>(std::max(1, radiusXZ));
                    const float normalizedY = static_cast<float>(y) / static_cast<float>(verticalExtent);
                    const float normalizedZ = static_cast<float>(z) / static_cast<float>(std::max(1, radiusXZ));
                    const float radialDistance = std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY + normalizedZ * normalizedZ);

                    uint64_t noiseSeed = Random::seed_from_ints({
                        static_cast<int64_t>(seed),
                        static_cast<int64_t>(center.x + x),
                        static_cast<int64_t>((center.y + y) * 7),
                        static_cast<int64_t>((center.z + z) * 13)
                    });
                    const float edgeNoise = static_cast<float>(noiseSeed & 1023ULL) / 1023.0f;
                    const float shellBias = 0.78f + (edgeNoise * 0.28f);

                    if (radialDistance > shellBias)
                    {
                        continue;
                    }

                    const glm::ivec3 worldPos = center + glm::ivec3(x, y, z);
                    if (worldPos.y < baseY)
                    {
                        continue;
                    }

                    edits.push_back(StructureBlockEdit{
                        .worldPosition = worldPos,
                        .block = make_cloud_block()
                    });
                }
            }
        }
    }
}

StructureType CloudStructureGenerator::type() const noexcept
{
    return StructureType::CLOUD;
}

std::vector<StructureBlockEdit> CloudStructureGenerator::generate(const StructureAnchor& anchor, const StructureGenerationContext& context) const
{
    const float blockWorldSize = context.terrainGenerator != nullptr ? context.terrainGenerator->block_world_size() : 1.0f;
    uint64_t seed = anchor.seed;
    const int baseRadius = world_units_to_voxels_round(
        static_cast<float>(Random::generate_from_seed(seed, 5, 8)),
        blockWorldSize);
    const int crownHeight = world_units_to_voxels_round(
        static_cast<float>(Random::generate_from_seed(seed, 3, 5)),
        blockWorldSize);
    const int oneUnit = world_units_to_voxels_round(1.0f, blockWorldSize);
    const int twoUnits = world_units_to_voxels_round(2.0f, blockWorldSize);
    const int threeUnits = world_units_to_voxels_round(3.0f, blockWorldSize);
    const int fourUnits = world_units_to_voxels_round(4.0f, blockWorldSize);
    const int sideReach = std::max(threeUnits, baseRadius - twoUnits);
    const glm::ivec3 cloudBase = anchor.worldOrigin;

    std::vector<StructureBlockEdit> edits{};
    edits.reserve(320);

    const auto add_seeded_puff = [&](const glm::ivec3 offset, const int minRadiusXZ, const int maxRadiusXZ, const int minRadiusY, const int maxRadiusY)
    {
        const glm::ivec3 jitter{
            world_offset_to_voxels_round(static_cast<float>(Random::generate_from_seed(seed, -1, 1)), blockWorldSize),
            world_offset_to_voxels_round(static_cast<float>(Random::generate_from_seed(seed, 0, 1)), blockWorldSize),
            world_offset_to_voxels_round(static_cast<float>(Random::generate_from_seed(seed, -1, 1)), blockWorldSize)
        };
        const int puffRadiusXZ = Random::generate_from_seed(seed, minRadiusXZ, maxRadiusXZ);
        const int puffRadiusY = Random::generate_from_seed(seed, minRadiusY, maxRadiusY);
        const uint64_t puffSeed = seed;
        add_cloud_puff(edits, cloudBase + offset + jitter, puffRadiusXZ, puffRadiusY, cloudBase.y, puffSeed);
    };

    add_seeded_puff(glm::ivec3(0, 0, 0), std::max(fourUnits, baseRadius - oneUnit), baseRadius + oneUnit, twoUnits, crownHeight);
    add_seeded_puff(glm::ivec3(sideReach, 0, 0), std::max(threeUnits, baseRadius - threeUnits), std::max(fourUnits, baseRadius - oneUnit), oneUnit, std::max(oneUnit, crownHeight - oneUnit));
    add_seeded_puff(glm::ivec3(-sideReach, 0, 0), std::max(threeUnits, baseRadius - threeUnits), std::max(fourUnits, baseRadius - oneUnit), oneUnit, std::max(oneUnit, crownHeight - oneUnit));
    add_seeded_puff(glm::ivec3(0, 0, sideReach), std::max(threeUnits, baseRadius - threeUnits), std::max(fourUnits, baseRadius - oneUnit), oneUnit, std::max(oneUnit, crownHeight - oneUnit));
    add_seeded_puff(glm::ivec3(0, 0, -sideReach), std::max(threeUnits, baseRadius - threeUnits), std::max(fourUnits, baseRadius - oneUnit), oneUnit, std::max(oneUnit, crownHeight - oneUnit));

    const int diagonalReach = std::max(twoUnits, baseRadius - threeUnits);
    add_seeded_puff(glm::ivec3(diagonalReach, oneUnit, diagonalReach), std::max(threeUnits, baseRadius - fourUnits), std::max(threeUnits, baseRadius - twoUnits), oneUnit, std::max(oneUnit, crownHeight - oneUnit));
    add_seeded_puff(glm::ivec3(diagonalReach, oneUnit, -diagonalReach), std::max(threeUnits, baseRadius - fourUnits), std::max(threeUnits, baseRadius - twoUnits), oneUnit, std::max(oneUnit, crownHeight - oneUnit));
    add_seeded_puff(glm::ivec3(-diagonalReach, oneUnit, diagonalReach), std::max(threeUnits, baseRadius - fourUnits), std::max(threeUnits, baseRadius - twoUnits), oneUnit, std::max(oneUnit, crownHeight - oneUnit));
    add_seeded_puff(glm::ivec3(-diagonalReach, oneUnit, -diagonalReach), std::max(threeUnits, baseRadius - fourUnits), std::max(threeUnits, baseRadius - twoUnits), oneUnit, std::max(oneUnit, crownHeight - oneUnit));

    add_seeded_puff(glm::ivec3(0, crownHeight, 0), std::max(twoUnits, baseRadius - fourUnits), std::max(threeUnits, baseRadius - twoUnits), oneUnit, twoUnits);

    return edits;
}

int CloudStructureGenerator::max_radius(const float blockWorldSize) const noexcept
{
    return world_units_to_voxels_ceil(CloudMaxRadiusWorld, blockWorldSize);
}

int CloudStructureGenerator::max_height(const float blockWorldSize) const noexcept
{
    return world_units_to_voxels_ceil(CloudMaxHeightWorld, blockWorldSize);
}

CloudPlacementStrategy::CloudPlacementStrategy(const CloudStructureGenerator& generator) :
    _generator(generator)
{
}

StructureType CloudPlacementStrategy::type() const noexcept
{
    return StructureType::CLOUD;
}

void CloudPlacementStrategy::collect_anchors(const StructureGenerationContext& context, std::vector<StructureAnchor>& anchors) const
{
    const float blockWorldSize =
        context.terrainGenerator != nullptr ?
        context.terrainGenerator->block_world_size() :
        1.0f;
    const int chunkVoxelWidth =
        context.terrainScaffold != nullptr ?
        context.terrainScaffold->chunkVoxelWidth :
        static_cast<int>(CHUNK_SIZE);
    const int chunkVoxelHeight =
        context.terrainGenerator != nullptr ?
        context.terrainGenerator->chunk_voxel_height() :
        static_cast<int>(CHUNK_HEIGHT);
    const int maxRadius = _generator.max_radius(blockWorldSize);
    const int maxHeight = _generator.max_height(blockWorldSize);
    const int placementCellSize = world_units_to_voxels_round(CloudPlacementCellSizeWorld, blockWorldSize);
    const int minBaseHeight = world_units_to_voxels_round(CloudMinBaseHeightWorld, blockWorldSize);
    const int maxBaseHeight = world_units_to_voxels_round(CloudMaxBaseHeightWorld, blockWorldSize);
    const int minWorldX = context.chunkOrigin.x - maxRadius;
    const int maxWorldX = context.chunkOrigin.x + chunkVoxelWidth - 1 + maxRadius;
    const int minWorldZ = context.chunkOrigin.y - maxRadius;
    const int maxWorldZ = context.chunkOrigin.y + chunkVoxelWidth - 1 + maxRadius;

    const int minCellX = floor_divide(minWorldX, placementCellSize);
    const int maxCellX = floor_divide(maxWorldX, placementCellSize);
    const int minCellZ = floor_divide(minWorldZ, placementCellSize);
    const int maxCellZ = floor_divide(maxWorldZ, placementCellSize);

    anchors.reserve(anchors.size() + static_cast<size_t>((maxCellX - minCellX + 1) * (maxCellZ - minCellZ + 1)));

    for (int cellX = minCellX; cellX <= maxCellX; ++cellX)
    {
        for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
        {
            uint64_t cellSeed = Random::seed_from_ints({ cellX, cellZ, placementCellSize, 7711 });
            if (Random::generate_from_seed(cellSeed, 0, 99) >= CloudSpawnChancePercent)
            {
                continue;
            }

            const int worldX = cellX * placementCellSize + Random::generate_from_seed(cellSeed, 0, placementCellSize - 1);
            const int worldZ = cellZ * placementCellSize + Random::generate_from_seed(cellSeed, 0, placementCellSize - 1);
            const int worldY = Random::generate_from_seed(cellSeed, minBaseHeight, maxBaseHeight);
            if (worldY + maxHeight >= chunkVoxelHeight)
            {
                continue;
            }

            anchors.push_back(StructureAnchor{
                .type = StructureType::CLOUD,
                .worldOrigin = { worldX, worldY, worldZ },
                .seed = Random::seed_from_ints({ cellX, cellZ, worldX, worldY, worldZ, 99173 })
            });
        }
    }
}
