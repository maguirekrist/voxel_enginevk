#include "world/structures/cloud_structure_generator.h"

#include <algorithm>
#include <cmath>

#include "constants.h"
#include "random.h"

namespace
{
    constexpr int CloudPlacementCellSize = 40;
    constexpr int CloudSpawnChancePercent = 42;
    constexpr int CloudMinBaseHeight = 148;
    constexpr int CloudMaxBaseHeight = 196;
    constexpr int CloudMaxRadius = 11;
    constexpr int CloudMaxHeight = 8;

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
                    if (worldPos.y < baseY || worldPos.y >= static_cast<int>(CHUNK_HEIGHT))
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

std::vector<StructureBlockEdit> CloudStructureGenerator::generate(const StructureAnchor& anchor, const StructureGenerationContext&) const
{
    uint64_t seed = anchor.seed;
    const int baseRadius = Random::generate_from_seed(seed, 5, 8);
    const int crownHeight = Random::generate_from_seed(seed, 3, 5);
    const int sideReach = std::max(3, baseRadius - 2);
    const glm::ivec3 cloudBase = anchor.worldOrigin;

    std::vector<StructureBlockEdit> edits{};
    edits.reserve(320);

    const auto add_seeded_puff = [&](const glm::ivec3 offset, const int minRadiusXZ, const int maxRadiusXZ, const int minRadiusY, const int maxRadiusY)
    {
        const glm::ivec3 jitter{
            Random::generate_from_seed(seed, -1, 1),
            Random::generate_from_seed(seed, 0, 1),
            Random::generate_from_seed(seed, -1, 1)
        };
        const int puffRadiusXZ = Random::generate_from_seed(seed, minRadiusXZ, maxRadiusXZ);
        const int puffRadiusY = Random::generate_from_seed(seed, minRadiusY, maxRadiusY);
        const uint64_t puffSeed = seed;
        add_cloud_puff(edits, cloudBase + offset + jitter, puffRadiusXZ, puffRadiusY, cloudBase.y, puffSeed);
    };

    add_seeded_puff(glm::ivec3(0, 0, 0), std::max(4, baseRadius - 1), baseRadius + 1, 2, crownHeight);
    add_seeded_puff(glm::ivec3(sideReach, 0, 0), std::max(3, baseRadius - 3), std::max(4, baseRadius - 1), 1, crownHeight - 1);
    add_seeded_puff(glm::ivec3(-sideReach, 0, 0), std::max(3, baseRadius - 3), std::max(4, baseRadius - 1), 1, crownHeight - 1);
    add_seeded_puff(glm::ivec3(0, 0, sideReach), std::max(3, baseRadius - 3), std::max(4, baseRadius - 1), 1, crownHeight - 1);
    add_seeded_puff(glm::ivec3(0, 0, -sideReach), std::max(3, baseRadius - 3), std::max(4, baseRadius - 1), 1, crownHeight - 1);

    const int diagonalReach = std::max(2, baseRadius - 3);
    add_seeded_puff(glm::ivec3(diagonalReach, 1, diagonalReach), std::max(3, baseRadius - 4), std::max(3, baseRadius - 2), 1, crownHeight - 1);
    add_seeded_puff(glm::ivec3(diagonalReach, 1, -diagonalReach), std::max(3, baseRadius - 4), std::max(3, baseRadius - 2), 1, crownHeight - 1);
    add_seeded_puff(glm::ivec3(-diagonalReach, 1, diagonalReach), std::max(3, baseRadius - 4), std::max(3, baseRadius - 2), 1, crownHeight - 1);
    add_seeded_puff(glm::ivec3(-diagonalReach, 1, -diagonalReach), std::max(3, baseRadius - 4), std::max(3, baseRadius - 2), 1, crownHeight - 1);

    add_seeded_puff(glm::ivec3(0, crownHeight, 0), std::max(2, baseRadius - 4), std::max(3, baseRadius - 2), 1, 2);

    return edits;
}

int CloudStructureGenerator::max_radius() const noexcept
{
    return CloudMaxRadius;
}

int CloudStructureGenerator::max_height() const noexcept
{
    return CloudMaxHeight;
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
    const int maxRadius = _generator.max_radius();
    const int maxHeight = _generator.max_height();
    const int minWorldX = context.chunkOrigin.x - maxRadius;
    const int maxWorldX = context.chunkOrigin.x + static_cast<int>(CHUNK_SIZE) - 1 + maxRadius;
    const int minWorldZ = context.chunkOrigin.y - maxRadius;
    const int maxWorldZ = context.chunkOrigin.y + static_cast<int>(CHUNK_SIZE) - 1 + maxRadius;

    const int minCellX = floor_divide(minWorldX, CloudPlacementCellSize);
    const int maxCellX = floor_divide(maxWorldX, CloudPlacementCellSize);
    const int minCellZ = floor_divide(minWorldZ, CloudPlacementCellSize);
    const int maxCellZ = floor_divide(maxWorldZ, CloudPlacementCellSize);

    anchors.reserve(anchors.size() + static_cast<size_t>((maxCellX - minCellX + 1) * (maxCellZ - minCellZ + 1)));

    for (int cellX = minCellX; cellX <= maxCellX; ++cellX)
    {
        for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
        {
            uint64_t cellSeed = Random::seed_from_ints({ cellX, cellZ, CloudPlacementCellSize, 7711 });
            if (Random::generate_from_seed(cellSeed, 0, 99) >= CloudSpawnChancePercent)
            {
                continue;
            }

            const int worldX = cellX * CloudPlacementCellSize + Random::generate_from_seed(cellSeed, 0, CloudPlacementCellSize - 1);
            const int worldZ = cellZ * CloudPlacementCellSize + Random::generate_from_seed(cellSeed, 0, CloudPlacementCellSize - 1);
            const int worldY = Random::generate_from_seed(cellSeed, CloudMinBaseHeight, CloudMaxBaseHeight);
            if (worldY + maxHeight >= static_cast<int>(CHUNK_HEIGHT))
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
