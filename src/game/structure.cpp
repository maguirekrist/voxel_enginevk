#include "structure.h"

#include <constants.h>
#include "random.h"
#include <world/terrain_gen.h>

namespace
{
    constexpr int TreePlacementCellSize = 12;
    constexpr int TreePlacementChancePercent = 40;
    constexpr int MaxTreeCanopyRadius = 3;
    constexpr int MaxTreeTrunkHeight = 6;

    Block make_block(const BlockType type, const bool solid, const uint8_t sunlight = 0)
    {
        return Block{
            ._solid = solid,
            ._sunlight = sunlight,
            ._type = static_cast<uint8_t>(type)
        };
    }

    StructureGenerator make_tree_generator()
    {
        return [](const StructureAnchor& anchor, const StructureGenerationContext&) -> std::vector<StructureBlockEdit>
        {
            std::vector<StructureBlockEdit> edits;

            uint64_t seed = anchor.seed;
            const int trunkHeight = Random::generate_from_seed(seed, 4, 6);
            const int canopyRadius = Random::generate_from_seed(seed, 2, 3);
            const int canopyBaseY = anchor.worldOrigin.y + trunkHeight - 2;
            const int canopyTopY = anchor.worldOrigin.y + trunkHeight + 1;

            edits.reserve(128);

            for (int y = 0; y < trunkHeight; ++y)
            {
                edits.push_back(StructureBlockEdit{
                    .worldPosition = anchor.worldOrigin + glm::ivec3(0, y, 0),
                    .block = make_block(BlockType::WOOD, true, 0)
                });
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

                        const glm::ivec3 worldPos = glm::ivec3(anchor.worldOrigin.x + x, y, anchor.worldOrigin.z + z);
                        if (worldPos == anchor.worldOrigin + glm::ivec3(0, trunkHeight - 1, 0))
                        {
                            continue;
                        }

                        edits.push_back(StructureBlockEdit{
                            .worldPosition = worldPos,
                            .block = make_block(BlockType::LEAVES, true, 0)
                        });
                    }
                }
            }

            edits.push_back(StructureBlockEdit{
                .worldPosition = anchor.worldOrigin + glm::ivec3(0, trunkHeight, 0),
                .block = make_block(BlockType::LEAVES, true, 0)
            });

            return edits;
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

    std::vector<StructureAnchor> collect_tree_anchors(const StructureGenerationContext& context)
    {
        std::vector<StructureAnchor> anchors;
        const int minWorldX = context.chunkOrigin.x - MaxTreeCanopyRadius;
        const int maxWorldX = context.chunkOrigin.x + static_cast<int>(CHUNK_SIZE) - 1 + MaxTreeCanopyRadius;
        const int minWorldZ = context.chunkOrigin.y - MaxTreeCanopyRadius;
        const int maxWorldZ = context.chunkOrigin.y + static_cast<int>(CHUNK_SIZE) - 1 + MaxTreeCanopyRadius;

        const int minCellX = floor_divide(minWorldX, TreePlacementCellSize);
        const int maxCellX = floor_divide(maxWorldX, TreePlacementCellSize);
        const int minCellZ = floor_divide(minWorldZ, TreePlacementCellSize);
        const int maxCellZ = floor_divide(maxWorldZ, TreePlacementCellSize);

        anchors.reserve(static_cast<size_t>((maxCellX - minCellX + 1) * (maxCellZ - minCellZ + 1)));

        for (int cellX = minCellX; cellX <= maxCellX; ++cellX)
        {
            for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
            {
                uint64_t cellSeed = Random::seed_from_ints({cellX, cellZ, TreePlacementCellSize});
                if (Random::generate_from_seed(cellSeed, 0, 99) >= TreePlacementChancePercent)
                {
                    continue;
                }

                const int worldX = cellX * TreePlacementCellSize + Random::generate_from_seed(cellSeed, 0, TreePlacementCellSize - 1);
                const int worldZ = cellZ * TreePlacementCellSize + Random::generate_from_seed(cellSeed, 0, TreePlacementCellSize - 1);
                const int terrainHeight = static_cast<int>(TerrainGenerator::instance().SampleHeight(worldX, worldZ));
                const bool canSpawnTree = terrainHeight > static_cast<int>(SEA_LEVEL) + 1 &&
                    terrainHeight + MaxTreeTrunkHeight + 2 < static_cast<int>(CHUNK_HEIGHT);

                if (!canSpawnTree)
                {
                    continue;
                }

                anchors.push_back(StructureAnchor{
                    .type = StructureType::TREE,
                    .worldOrigin = { worldX, terrainHeight + 1, worldZ },
                    .seed = Random::seed_from_ints({ cellX, cellZ, worldX, worldZ, terrainHeight }),
                    .treeVariant = TreeVariant::Oak
                });
            }
        }

        return anchors;
    }
}

StructureRegistry& StructureRegistry::instance()
{
    static StructureRegistry instance;
    return instance;
}

std::vector<StructureBlockEdit> StructureRegistry::generate(const StructureAnchor& anchor, const StructureGenerationContext& context) const
{
    const auto it = _generators.find(anchor.type);
    if (it == _generators.end())
    {
        return {};
    }

    return it->second(anchor, context);
}

std::vector<StructureBlockEdit> StructureRegistry::generate_overlapping(const StructureGenerationContext& context) const
{
    std::vector<StructureBlockEdit> edits;
    const std::vector<StructureAnchor> anchors = collect_tree_anchors(context);
    edits.reserve(anchors.size() * 96);

    for (const StructureAnchor& anchor : anchors)
    {
        std::vector<StructureBlockEdit> generated = generate(anchor, context);
        edits.insert(edits.end(), generated.begin(), generated.end());
    }

    return edits;
}

StructureRegistry::StructureRegistry()
{
    _generators.emplace(StructureType::TREE, make_tree_generator());
}
