#include <limits>
#include <memory>

#include <gtest/gtest.h>

#include "test_support.h"
#include "world/structures/cloud_structure_generator.h"
#include "game/decoration.h"
#include "game/player_entity.h"
#include "world/structures/tree_structure_generator.h"
#include "render/chunk_decoration_render_registry.h"
#include "world/terrain_gen.h"

using test_support::contains_block;

TEST(PlayerEntityTest, WorldBoundsUseFeetPositionAsBase)
{
    PlayerEntity player{ glm::vec3(4.0f, 10.0f, -2.0f) };
    const AABB bounds = player.world_bounds();

    EXPECT_NEAR(bounds.min.x, 3.65f, 0.0001f);
    EXPECT_NEAR(bounds.min.y, 10.0f, 0.0001f);
    EXPECT_NEAR(bounds.min.z, -2.35f, 0.0001f);
    EXPECT_NEAR(bounds.max.x, 4.35f, 0.0001f);
    EXPECT_NEAR(bounds.max.y, 11.8f, 0.0001f);
    EXPECT_NEAR(bounds.max.z, -1.65f, 0.0001f);
}

TEST(PlayerEntityTest, AssemblyRenderComponentStartsWithIdentityRotation)
{
    PlayerEntity player{ glm::vec3(4.0f, 10.0f, -2.0f) };

    const glm::quat rotation = player.assembly_render_component().rotation;
    EXPECT_NEAR(rotation.w, 1.0f, 0.0001f);
    EXPECT_NEAR(rotation.x, 0.0f, 0.0001f);
    EXPECT_NEAR(rotation.y, 0.0f, 0.0001f);
    EXPECT_NEAR(rotation.z, 0.0f, 0.0001f);
}

TEST(DecorationPlacementTest, ForestFlowersRequireForestBiome)
{
    EXPECT_TRUE(decoration::is_forest_flower_biome(BiomeType::Forest));
    EXPECT_FALSE(decoration::is_forest_flower_biome(BiomeType::Plains));
    EXPECT_FALSE(decoration::is_forest_flower_biome(BiomeType::River));
}

TEST(DecorationPlacementTest, SurfaceDecorationRequiresGroundAndAirClearance)
{
    ChunkData chunk{
        .coord = ChunkCoord{0, 0},
        .position = glm::ivec2(0, 0)
    };

    for (int x = 0; x < CHUNK_SIZE; ++x)
    {
        for (int y = 0; y < CHUNK_HEIGHT; ++y)
        {
            for (int z = 0; z < CHUNK_SIZE; ++z)
            {
                chunk.blocks[x][y][z] = Block{
                    ._solid = false,
                    ._sunlight = 0,
                    ._type = static_cast<uint8_t>(BlockType::AIR)
                };
            }
        }
    }

    chunk.blocks[2][10][3] = Block{
        ._solid = true,
        ._sunlight = 0,
        ._type = static_cast<uint8_t>(BlockType::GROUND)
    };

    const TerrainColumnSample forestColumn{
        .surfaceHeight = 10,
        .stoneHeight = 8,
        .biome = BiomeType::Forest,
        .topBlock = BlockType::GROUND,
        .fillerBlock = BlockType::GROUND
    };

    EXPECT_TRUE(decoration::can_place_surface_decoration(chunk, forestColumn, glm::ivec3(2, 10, 3), 2));

    chunk.blocks[2][11][3] = Block{
        ._solid = true,
        ._sunlight = 0,
        ._type = static_cast<uint8_t>(BlockType::WOOD)
    };
    EXPECT_FALSE(decoration::can_place_surface_decoration(chunk, forestColumn, glm::ivec3(2, 10, 3), 2));

    chunk.blocks[2][11][3] = Block{
        ._solid = false,
        ._sunlight = 0,
        ._type = static_cast<uint8_t>(BlockType::AIR)
    };
    chunk.blocks[2][10][3] = Block{
        ._solid = true,
        ._sunlight = 0,
        ._type = static_cast<uint8_t>(BlockType::SAND)
    };
    EXPECT_FALSE(decoration::can_place_surface_decoration(chunk, forestColumn, glm::ivec3(2, 10, 3), 2));
}

TEST(ChunkDecorationRenderRegistryTest, RebuildsWhenGeneratedChunkDataReplacesPlaceholderData)
{
    Chunk chunk{ ChunkCoord{0, 0} };
    chunk._gen.store(7, std::memory_order::release);
    chunk._state.store(ChunkState::Generated, std::memory_order::release);

    const ChunkDecorationRenderRegistry::ChunkDecorationEntryState placeholderEntry{
        .chunk = &chunk,
        .data = chunk._data.get(),
        .generationId = 7
    };

    EXPECT_FALSE(ChunkDecorationRenderRegistry::requires_rebuild(&placeholderEntry, chunk));

    auto generatedData = std::make_shared<ChunkData>(ChunkCoord{0, 0}, glm::ivec2(0, 0));
    generatedData->voxelDecorations.push_back(VoxelDecorationPlacement{
        .assetId = "flower",
        .worldPosition = glm::vec3(0.5f, 8.0f, 0.5f),
        .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        .scale = 1.0f
    });
    chunk._data = generatedData;

    EXPECT_TRUE(ChunkDecorationRenderRegistry::requires_rebuild(&placeholderEntry, chunk));
}

TEST(TreeStructureGeneratorTest, GiantTreeBuildsTwoByTwoTrunk)
{
    TreeStructureGenerator generator;
    const StructureAnchor anchor{
        .type = StructureType::TREE,
        .worldOrigin = {32, 90, 48},
        .seed = 424242,
        .treeVariant = TreeVariant::Giant
    };
    const StructureGenerationContext context{};

    const std::vector<StructureBlockEdit> edits = generator.generate(anchor, context);

    EXPECT_TRUE(contains_block(edits, anchor.worldOrigin + glm::ivec3(0, 0, 0), BlockType::WOOD));
    EXPECT_TRUE(contains_block(edits, anchor.worldOrigin + glm::ivec3(1, 0, 0), BlockType::WOOD));
    EXPECT_TRUE(contains_block(edits, anchor.worldOrigin + glm::ivec3(0, 0, 1), BlockType::WOOD));
    EXPECT_TRUE(contains_block(edits, anchor.worldOrigin + glm::ivec3(1, 0, 1), BlockType::WOOD));
    EXPECT_TRUE(contains_block(edits, anchor.worldOrigin + glm::ivec3(0, 5, 0), BlockType::WOOD));
    EXPECT_TRUE(contains_block(edits, anchor.worldOrigin + glm::ivec3(1, 5, 1), BlockType::WOOD));
}

TEST(TreeStructureGeneratorTest, OakTreePreservesFullTrunkColumn)
{
    TreeStructureGenerator generator;
    const StructureAnchor anchor{
        .type = StructureType::TREE,
        .worldOrigin = {24, 84, 24},
        .seed = 1337,
        .treeVariant = TreeVariant::Oak
    };
    const StructureGenerationContext context{};

    const std::vector<StructureBlockEdit> edits = generator.generate(anchor, context);
    int maxLeafY = std::numeric_limits<int>::min();

    for (int y = 0; y < 6; ++y)
    {
        const glm::ivec3 trunkPos = anchor.worldOrigin + glm::ivec3(0, y, 0);
        const bool hasWood = contains_block(edits, trunkPos, BlockType::WOOD);
        const bool hasLeaves = contains_block(edits, trunkPos, BlockType::LEAVES);

        if (hasWood)
        {
            EXPECT_FALSE(hasLeaves);
        }
    }

    for (const StructureBlockEdit& edit : edits)
    {
        if (edit.block._type == BlockType::LEAVES)
        {
            maxLeafY = std::max(maxLeafY, edit.worldPosition.y);
        }
    }

    EXPECT_GE(maxLeafY, anchor.worldOrigin.y + 2);
    EXPECT_LE(maxLeafY, anchor.worldOrigin.y + 8);
}

TEST(CloudStructureGeneratorTest, CloudBuildsFlatBottomPuffyVolume)
{
    CloudStructureGenerator generator;
    const StructureAnchor anchor{
        .type = StructureType::CLOUD,
        .worldOrigin = { 48, 164, 48 },
        .seed = 882244
    };
    const StructureGenerationContext context{};

    const std::vector<StructureBlockEdit> edits = generator.generate(anchor, context);
    ASSERT_FALSE(edits.empty());

    int minY = std::numeric_limits<int>::max();
    int maxY = std::numeric_limits<int>::min();
    int baseLayerBlocks = 0;

    for (const StructureBlockEdit& edit : edits)
    {
        EXPECT_EQ(edit.block._type, BlockType::CLOUD);
        minY = std::min(minY, edit.worldPosition.y);
        maxY = std::max(maxY, edit.worldPosition.y);
        if (edit.worldPosition.y == anchor.worldOrigin.y)
        {
            ++baseLayerBlocks;
        }
    }

    EXPECT_EQ(minY, anchor.worldOrigin.y);
    EXPECT_GT(maxY, anchor.worldOrigin.y);
    EXPECT_GE(baseLayerBlocks, 12);
}
