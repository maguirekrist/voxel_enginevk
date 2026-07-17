#include "world/structures/tree_structure_generator.h"

#include <algorithm>
#include <cmath>

#include "random.h"
#include "world/terrain_gen.h"

namespace
{
    constexpr float TreePlacementCellSizeWorld = 12.0f;
    constexpr int ForestTreeChancePercent = 52;
    constexpr int PlainsTreeChancePercent = 18;
    constexpr int MountainTreeChancePercent = 10;
    constexpr int ForestGiantTreeChancePercent = 24;
    constexpr int PlainsGiantTreeChancePercent = 8;

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

    [[nodiscard]] int centered_min_offset(const int width) noexcept
    {
        return -((width - 1) / 2);
    }

    [[nodiscard]] Block make_structure_block(const BlockType type, const bool solid)
    {
        return Block{
            ._solid = solid,
            ._sunlight = 0,
            ._type = static_cast<uint8_t>(type)
        };
    }

    void add_leaf_blob(
        std::vector<StructureBlockEdit>& edits,
        const glm::ivec3 center,
        const int radiusXZ,
        const int radiusY,
        const auto& skipPredicate)
    {
        for (int y = -radiusY; y <= radiusY; ++y)
        {
            const float verticalBlend = radiusY > 0 ? static_cast<float>(std::abs(y)) / static_cast<float>(radiusY) : 0.0f;
            const int layerRadius = std::max(1, radiusXZ - static_cast<int>(std::round(verticalBlend * 2.0f)));

            for (int x = -layerRadius; x <= layerRadius; ++x)
            {
                for (int z = -layerRadius; z <= layerRadius; ++z)
                {
                    const float normalized =
                        (static_cast<float>(x * x + z * z) / static_cast<float>(std::max(1, layerRadius * layerRadius))) +
                        (radiusY > 0 ? (static_cast<float>(y * y) / static_cast<float>(radiusY * radiusY)) : 0.0f);

                    if (normalized > 1.35f)
                    {
                        continue;
                    }

                    const glm::ivec3 worldPos = center + glm::ivec3(x, y, z);
                    if (skipPredicate(worldPos))
                    {
                        continue;
                    }

                    edits.push_back(StructureBlockEdit{
                        .worldPosition = worldPos,
                        .block = make_structure_block(BlockType::LEAVES, true)
                    });
                }
            }
        }
    }

    void add_leaf_puff(
        std::vector<StructureBlockEdit>& edits,
        const glm::ivec3 center,
        const int radiusXZ,
        const int radiusY,
        const uint64_t shapeSeed,
        const auto& skipPredicate)
    {
        const int verticalExtent = std::max(1, radiusY);
        const int horizontalExtent = std::max(1, radiusXZ + 1);

        for (int y = -verticalExtent; y <= verticalExtent; ++y)
        {
            for (int x = -horizontalExtent; x <= horizontalExtent; ++x)
            {
                for (int z = -horizontalExtent; z <= horizontalExtent; ++z)
                {
                    const float normalizedX = static_cast<float>(x) / static_cast<float>(std::max(1, radiusXZ));
                    const float normalizedY = static_cast<float>(y) / static_cast<float>(verticalExtent);
                    const float normalizedZ = static_cast<float>(z) / static_cast<float>(std::max(1, radiusXZ));
                    const float radialDistance = std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY + normalizedZ * normalizedZ);

                    const uint64_t noiseSeed = Random::seed_from_ints({
                        static_cast<int64_t>(shapeSeed),
                        static_cast<int64_t>(center.x + x),
                        static_cast<int64_t>((center.y + y) * 3),
                        static_cast<int64_t>((center.z + z) * 5)
                    });
                    const float edgeNoise = static_cast<float>(noiseSeed & 1023ULL) / 1023.0f;
                    const float shellBias = 0.82f + edgeNoise * 0.42f;

                    if (radialDistance > shellBias)
                    {
                        continue;
                    }

                    const bool nearCenterColumn = std::abs(x) <= 1 && std::abs(z) <= 1;
                    if (nearCenterColumn && y < -verticalExtent / 2 && radialDistance > 0.58f + edgeNoise * 0.12f)
                    {
                        continue;
                    }

                    const glm::ivec3 worldPos = center + glm::ivec3(x, y, z);
                    if (skipPredicate(worldPos))
                    {
                        continue;
                    }

                    edits.push_back(StructureBlockEdit{
                        .worldPosition = worldPos,
                        .block = make_structure_block(BlockType::LEAVES, true)
                    });
                }
            }
        }
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

    class OakTreeVariantGenerator final : public ITreeVariantGenerator
    {
    public:
        [[nodiscard]] TreeVariant variant() const noexcept override
        {
            return TreeVariant::Oak;
        }

        [[nodiscard]] float max_radius_world() const noexcept override
        {
            return 3.0f;
        }

        [[nodiscard]] float max_height_world() const noexcept override
        {
            return 8.0f;
        }

        void append_structure(
            const StructureAnchor& anchor,
            const float blockWorldSize,
            std::vector<StructureBlockEdit>& edits) const override
        {
            uint64_t seed = anchor.seed;
            const int trunkWidth = world_units_to_voxels_round(1.0f, blockWorldSize);
            const int trunkHeight = world_units_to_voxels_round(
                static_cast<float>(Random::generate_from_seed(seed, 4, 6)),
                blockWorldSize);
            const int canopyRadius = world_units_to_voxels_round(
                static_cast<float>(Random::generate_from_seed(seed, 2, 3)),
                blockWorldSize);
            const int canopyBaseOffset = world_units_to_voxels_round(2.0f, blockWorldSize);
            const int canopyTopOffset = world_units_to_voxels_round(1.0f, blockWorldSize);
            const int canopyBaseY = anchor.worldOrigin.y + trunkHeight - canopyBaseOffset;
            const int canopyTopY = anchor.worldOrigin.y + trunkHeight + canopyTopOffset;
            const int trunkMinOffset = centered_min_offset(trunkWidth);
            const auto is_trunk_position = [&](const glm::ivec3& worldPos)
            {
                const int localX = worldPos.x - (anchor.worldOrigin.x + trunkMinOffset);
                const int localZ = worldPos.z - (anchor.worldOrigin.z + trunkMinOffset);
                return localX >= 0 &&
                    localX < trunkWidth &&
                    localZ >= 0 &&
                    localZ < trunkWidth &&
                    worldPos.y >= anchor.worldOrigin.y &&
                    worldPos.y < anchor.worldOrigin.y + trunkHeight;
            };

            edits.reserve(edits.size() + 128);

            for (int y = 0; y < trunkHeight; ++y)
            {
                for (int dx = 0; dx < trunkWidth; ++dx)
                {
                    for (int dz = 0; dz < trunkWidth; ++dz)
                    {
                        edits.push_back(StructureBlockEdit{
                            .worldPosition = anchor.worldOrigin + glm::ivec3(trunkMinOffset + dx, y, trunkMinOffset + dz),
                            .block = make_structure_block(BlockType::WOOD, true)
                        });
                    }
                }
            }

            for (int y = canopyBaseY; y <= canopyTopY; ++y)
            {
                const int layerOffset = y - (anchor.worldOrigin.y + trunkHeight);
                const int layerRadius = std::max(1, canopyRadius - std::abs(layerOffset));

                for (int x = -layerRadius; x <= layerRadius; ++x)
                {
                    for (int z = -layerRadius; z <= layerRadius; ++z)
                    {
                        const int distance = std::abs(x) + std::abs(z);
                        const bool isCorner = std::abs(x) == layerRadius && std::abs(z) == layerRadius;
                        if (distance > layerRadius + 1 || (isCorner && layerRadius > 1))
                        {
                            continue;
                        }

                        const glm::ivec3 worldPos{
                            anchor.worldOrigin.x + x,
                            y,
                            anchor.worldOrigin.z + z
                        };
                        if (is_trunk_position(worldPos))
                        {
                            continue;
                        }

                        edits.push_back(StructureBlockEdit{
                            .worldPosition = worldPos,
                            .block = make_structure_block(BlockType::LEAVES, true)
                        });
                    }
                }
            }

            edits.push_back(StructureBlockEdit{
                .worldPosition = anchor.worldOrigin + glm::ivec3(0, trunkHeight, 0),
                .block = make_structure_block(BlockType::LEAVES, true)
            });
        }
    };

    class GiantTreeVariantGenerator final : public ITreeVariantGenerator
    {
    public:
        [[nodiscard]] TreeVariant variant() const noexcept override
        {
            return TreeVariant::Giant;
        }

        [[nodiscard]] float max_radius_world() const noexcept override
        {
            return 7.0f;
        }

        [[nodiscard]] float max_height_world() const noexcept override
        {
            return 15.0f;
        }

        void append_structure(
            const StructureAnchor& anchor,
            const float blockWorldSize,
            std::vector<StructureBlockEdit>& edits) const override
        {
            uint64_t seed = anchor.seed;
            const int trunkWidth = world_units_to_voxels_round(2.0f, blockWorldSize);
            const int trunkHeight = world_units_to_voxels_round(
                static_cast<float>(Random::generate_from_seed(seed, 10, 15)),
                blockWorldSize);
            const int crownRadius = world_units_to_voxels_round(
                static_cast<float>(Random::generate_from_seed(seed, 4, 6)),
                blockWorldSize);
            const int crownHeight = world_units_to_voxels_round(
                static_cast<float>(Random::generate_from_seed(seed, 3, 4)),
                blockWorldSize);
            const glm::ivec3 trunkBase = anchor.worldOrigin;
            const int trunkMinOffset = centered_min_offset(trunkWidth);
            const int oneUnit = world_units_to_voxels_round(1.0f, blockWorldSize);
            const int twoUnits = world_units_to_voxels_round(2.0f, blockWorldSize);
            const int threeUnits = world_units_to_voxels_round(3.0f, blockWorldSize);

            const auto is_trunk_position = [&](const glm::ivec3& worldPos)
            {
                if (worldPos.y < trunkBase.y || worldPos.y >= trunkBase.y + trunkHeight)
                {
                    return false;
                }

                const int localX = worldPos.x - (trunkBase.x + trunkMinOffset);
                const int localZ = worldPos.z - (trunkBase.z + trunkMinOffset);
                return localX >= 0 && localX < trunkWidth && localZ >= 0 && localZ < trunkWidth;
            };

            edits.reserve(edits.size() + 640);

            for (int y = 0; y < trunkHeight; ++y)
            {
                for (int dx = 0; dx < trunkWidth; ++dx)
                {
                    for (int dz = 0; dz < trunkWidth; ++dz)
                    {
                        edits.push_back(StructureBlockEdit{
                            .worldPosition = trunkBase + glm::ivec3(trunkMinOffset + dx, y, trunkMinOffset + dz),
                            .block = make_structure_block(BlockType::WOOD, true)
                        });
                    }
                }
            }

            const int crownCenterOffset = trunkMinOffset + (trunkWidth / 2);
            const glm::ivec3 crownCenter = trunkBase + glm::ivec3(crownCenterOffset, trunkHeight - 1, crownCenterOffset);
            const auto add_seeded_puff = [&](const glm::ivec3 baseOffset, const int minRadiusXZ, const int maxRadiusXZ, const int minRadiusY, const int maxRadiusY)
            {
                const glm::ivec3 jitter{
                    world_offset_to_voxels_round(static_cast<float>(Random::generate_from_seed(seed, -1, 1)), blockWorldSize),
                    world_offset_to_voxels_round(static_cast<float>(Random::generate_from_seed(seed, -1, 1)), blockWorldSize),
                    world_offset_to_voxels_round(static_cast<float>(Random::generate_from_seed(seed, -1, 1)), blockWorldSize)
                };
                const int puffRadiusXZ = Random::generate_from_seed(seed, minRadiusXZ, maxRadiusXZ);
                const int puffRadiusY = Random::generate_from_seed(seed, minRadiusY, maxRadiusY);
                const uint64_t puffSeed = seed;

                add_leaf_puff(edits, crownCenter + baseOffset + jitter, puffRadiusXZ, puffRadiusY, puffSeed, is_trunk_position);
            };

            add_seeded_puff(glm::ivec3(0, 0, 0), std::max(threeUnits, crownRadius - oneUnit), crownRadius, crownHeight, crownHeight + oneUnit);
            add_seeded_puff(glm::ivec3(0, crownHeight - oneUnit, 0), std::max(threeUnits, crownRadius - twoUnits), std::max(threeUnits, crownRadius - oneUnit), oneUnit, twoUnits);
            add_seeded_puff(glm::ivec3(0, -twoUnits, 0), std::max(threeUnits, crownRadius - twoUnits), std::max(threeUnits, crownRadius - oneUnit), oneUnit, twoUnits);

            const int sideReach = std::max(threeUnits, crownRadius - oneUnit);
            add_seeded_puff(glm::ivec3(sideReach, 0, 0), std::max(threeUnits, crownRadius - twoUnits), sideReach, std::max(twoUnits, crownHeight - oneUnit), crownHeight);
            add_seeded_puff(glm::ivec3(-sideReach, 0, 0), std::max(threeUnits, crownRadius - twoUnits), sideReach, std::max(twoUnits, crownHeight - oneUnit), crownHeight);
            add_seeded_puff(glm::ivec3(0, 0, sideReach), std::max(threeUnits, crownRadius - twoUnits), sideReach, std::max(twoUnits, crownHeight - oneUnit), crownHeight);
            add_seeded_puff(glm::ivec3(0, 0, -sideReach), std::max(threeUnits, crownRadius - twoUnits), sideReach, std::max(twoUnits, crownHeight - oneUnit), crownHeight);

            const int diagonalReach = std::max(twoUnits, crownRadius - twoUnits);
            add_seeded_puff(glm::ivec3(diagonalReach, oneUnit, diagonalReach), std::max(twoUnits, crownRadius - threeUnits), std::max(threeUnits, crownRadius - oneUnit), oneUnit, std::max(twoUnits, crownHeight - oneUnit));
            add_seeded_puff(glm::ivec3(diagonalReach, 0, -diagonalReach), std::max(twoUnits, crownRadius - threeUnits), std::max(threeUnits, crownRadius - oneUnit), oneUnit, std::max(twoUnits, crownHeight - oneUnit));
            add_seeded_puff(glm::ivec3(-diagonalReach, oneUnit, diagonalReach), std::max(twoUnits, crownRadius - threeUnits), std::max(threeUnits, crownRadius - oneUnit), oneUnit, std::max(twoUnits, crownHeight - oneUnit));
            add_seeded_puff(glm::ivec3(-diagonalReach, 0, -diagonalReach), std::max(twoUnits, crownRadius - threeUnits), std::max(threeUnits, crownRadius - oneUnit), oneUnit, std::max(twoUnits, crownHeight - oneUnit));
        }
    };

    [[nodiscard]] int tree_spawn_chance_for_biome(const BiomeType biome)
    {
        switch (biome)
        {
        case BiomeType::Forest:
            return ForestTreeChancePercent;
        case BiomeType::Mountains:
            return MountainTreeChancePercent;
        case BiomeType::Plains:
            return PlainsTreeChancePercent;
        default:
            return 0;
        }
    }

    [[nodiscard]] int giant_tree_chance_for_biome(const BiomeType biome)
    {
        switch (biome)
        {
        case BiomeType::Forest:
            return ForestGiantTreeChancePercent;
        case BiomeType::Plains:
            return PlainsGiantTreeChancePercent;
        default:
            return 0;
        }
    }

}

TreeStructureGenerator::TreeStructureGenerator()
{
    _variantGenerators.emplace(TreeVariant::Oak, std::make_unique<OakTreeVariantGenerator>());
    _variantGenerators.emplace(TreeVariant::Giant, std::make_unique<GiantTreeVariantGenerator>());
}

StructureType TreeStructureGenerator::type() const noexcept
{
    return StructureType::TREE;
}

std::vector<StructureBlockEdit> TreeStructureGenerator::generate(const StructureAnchor& anchor, const StructureGenerationContext& context) const
{
    const auto it = _variantGenerators.find(anchor.treeVariant);
    if (it == _variantGenerators.end())
    {
        return {};
    }

    std::vector<StructureBlockEdit> edits;
    const float blockWorldSize = context.terrainGenerator != nullptr ? context.terrainGenerator->block_world_size() : 1.0f;
    it->second->append_structure(anchor, blockWorldSize, edits);
    return edits;
}

int TreeStructureGenerator::max_variant_radius(const float blockWorldSize) const noexcept
{
    int radius = 0;
    for (const auto& [_, generator] : _variantGenerators)
    {
        radius = std::max(radius, world_units_to_voxels_ceil(generator->max_radius_world(), blockWorldSize));
    }

    return radius;
}

int TreeStructureGenerator::max_variant_height(const float blockWorldSize) const noexcept
{
    int height = 0;
    for (const auto& [_, generator] : _variantGenerators)
    {
        height = std::max(height, world_units_to_voxels_ceil(generator->max_height_world(), blockWorldSize));
    }

    return height;
}

TreePlacementStrategy::TreePlacementStrategy(const TreeStructureGenerator& generator) :
    _generator(generator)
{
}

StructureType TreePlacementStrategy::type() const noexcept
{
    return StructureType::TREE;
}

void TreePlacementStrategy::collect_anchors(const StructureGenerationContext& context, std::vector<StructureAnchor>& anchors) const
{
    if (context.terrainGenerator == nullptr)
    {
        return;
    }

    const float blockWorldSize = context.terrainGenerator->block_world_size();
    const int chunkVoxelWidth =
        context.terrainScaffold != nullptr ?
        context.terrainScaffold->chunkVoxelWidth :
        context.terrainGenerator->chunk_voxel_width();
    const int chunkVoxelHeight = context.terrainGenerator->chunk_voxel_height();
    const int maxRadius = _generator.max_variant_radius(blockWorldSize);
    const int maxHeight = _generator.max_variant_height(blockWorldSize);
    const int placementCellSize = world_units_to_voxels_round(TreePlacementCellSizeWorld, blockWorldSize);
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
            uint64_t cellSeed = Random::seed_from_ints({cellX, cellZ, placementCellSize});
            const int worldX = cellX * placementCellSize + Random::generate_from_seed(cellSeed, 0, placementCellSize - 1);
            const int worldZ = cellZ * placementCellSize + Random::generate_from_seed(cellSeed, 0, placementCellSize - 1);

            const TerrainColumnSample column = context.terrainGenerator->SampleColumn(worldX, worldZ);
            const int treeSpawnChance = tree_spawn_chance_for_biome(column.biome);
            if (treeSpawnChance == 0 || Random::generate_from_seed(cellSeed, 0, 99) >= treeSpawnChance)
            {
                continue;
            }

            const TreeVariant variant = Random::generate_from_seed(cellSeed, 0, 99) < giant_tree_chance_for_biome(column.biome)
                ? TreeVariant::Giant
                : TreeVariant::Oak;

            const int topPadding = world_units_to_voxels_ceil(variant == TreeVariant::Giant ? 6.0f : 2.0f, blockWorldSize);
            const bool canSpawnTree =
                column.surfaceHeight > TerrainGenerator::sea_level() &&
                !column.isBeach &&
                column.biome != BiomeType::Ocean &&
                column.surfaceHeight + maxHeight + topPadding < chunkVoxelHeight;

            if (!canSpawnTree)
            {
                continue;
            }

            anchors.push_back(StructureAnchor{
                .type = StructureType::TREE,
                .worldOrigin = { worldX, column.surfaceHeight + 1, worldZ },
                .seed = Random::seed_from_ints({ cellX, cellZ, worldX, worldZ, column.surfaceHeight, static_cast<int>(column.biome) }),
                .treeVariant = variant
            });
        }
    }
}
