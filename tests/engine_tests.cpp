#include <memory>
#include <limits>
#include <ranges>

#include <gtest/gtest.h>

#include "constants.h"
#include "game/block.h"
#include "game/chunk.h"
#include "game/tree_structure_generator.h"
#include "game/world.h"
#include "world/chunk_lighting.h"
#include "world/chunk_manager.h"
#include "world/terrain_gen.h"

Chunk* ChunkManager::get_chunk(const ChunkCoord) const
{
    return nullptr;
}

namespace
{
    std::shared_ptr<ChunkData> make_empty_chunk(const ChunkCoord coord)
    {
        auto chunk = std::make_shared<ChunkData>();
        chunk->coord = coord;
        chunk->position = glm::ivec2(coord.x * CHUNK_SIZE, coord.z * CHUNK_SIZE);

        for (int x = 0; x < CHUNK_SIZE; ++x)
        {
            for (int y = 0; y < CHUNK_HEIGHT; ++y)
            {
                for (int z = 0; z < CHUNK_SIZE; ++z)
                {
                    chunk->blocks[x][y][z] = Block{
                        ._solid = false,
                        ._sunlight = 0,
                        ._type = BlockType::AIR
                    };
                }
            }
        }

        return chunk;
    }

    ChunkNeighborhood make_empty_neighborhood(const std::shared_ptr<ChunkData>& center)
    {
        return ChunkNeighborhood{
            .center = center,
            .north = make_empty_chunk({0, 1}),
            .south = make_empty_chunk({0, -1}),
            .east = make_empty_chunk({-1, 0}),
            .west = make_empty_chunk({1, 0}),
            .northEast = make_empty_chunk({-1, 1}),
            .northWest = make_empty_chunk({1, 1}),
            .southEast = make_empty_chunk({-1, -1}),
            .southWest = make_empty_chunk({1, -1})
        };
    }

    bool contains_block(
        const std::vector<StructureBlockEdit>& edits,
        const glm::ivec3 worldPosition,
        const BlockType blockType)
    {
        return std::ranges::any_of(edits, [&](const StructureBlockEdit& edit)
        {
            return edit.worldPosition == worldPosition && edit.block._type == blockType;
        });
    }
}

TEST(WorldCoordinatesTest, WrapsPositiveAndNegativeCoordinatesCorrectly)
{
    EXPECT_EQ(World::get_chunk_coordinates(glm::vec3(0.0f, 0.0f, 0.0f)), (ChunkCoord{0, 0}));
    EXPECT_EQ(World::get_chunk_coordinates(glm::vec3(15.9f, 10.0f, 15.9f)), (ChunkCoord{0, 0}));
    EXPECT_EQ(World::get_chunk_coordinates(glm::vec3(16.0f, 10.0f, 16.0f)), (ChunkCoord{1, 1}));
    EXPECT_EQ(World::get_chunk_coordinates(glm::vec3(-0.1f, 10.0f, -0.1f)), (ChunkCoord{-1, -1}));

    const glm::ivec3 wrappedNegative = World::get_local_coordinates(glm::vec3(-1.0f, 5.0f, -1.0f));
    EXPECT_EQ(wrappedNegative.x, CHUNK_SIZE - 1);
    EXPECT_EQ(wrappedNegative.z, CHUNK_SIZE - 1);

    const glm::ivec2 negativeOrigin = World::get_chunk_origin(glm::vec3(-1.0f, 0.0f, -1.0f));
    EXPECT_EQ(negativeOrigin.x, -static_cast<int>(CHUNK_SIZE));
    EXPECT_EQ(negativeOrigin.y, -static_cast<int>(CHUNK_SIZE));
}

TEST(ChunkLightingTest, SkylightDistinguishesOpenSkyFromRoofedCells)
{
    auto center = make_empty_chunk({0, 0});
    ChunkNeighborhood neighborhood = make_empty_neighborhood(center);

    constexpr int testX = 8;
    constexpr int testZ = 8;
    constexpr int roofY = 80;

    const auto litOpenSky = ChunkLighting::solve_skylight(neighborhood);
    ASSERT_NE(litOpenSky, nullptr);
    EXPECT_EQ(litOpenSky->blocks[testX][roofY][testZ]._sunlight, MAX_LIGHT_LEVEL);

    for (int x = testX - 2; x <= testX + 2; ++x)
    {
        for (int z = testZ - 2; z <= testZ + 2; ++z)
        {
            center->blocks[x][roofY][z] = Block{
                ._solid = true,
                ._sunlight = 0,
                ._type = BlockType::STONE
            };
        }
    }

    const auto litRoofed = ChunkLighting::solve_skylight(neighborhood);
    ASSERT_NE(litRoofed, nullptr);
    EXPECT_LT(litRoofed->blocks[testX][roofY - 1][testZ]._sunlight, MAX_LIGHT_LEVEL);
}

TEST(ChunkLightingTest, LampLocalLightPropagatesAndIsBlockedBySolidWall)
{
    auto center = make_empty_chunk({0, 0});
    ChunkNeighborhood neighborhood = make_empty_neighborhood(center);

    constexpr int lampX = 8;
    constexpr int lampY = 40;
    constexpr int lampZ = 8;

    center->blocks[lampX][lampY][lampZ] = Block{
        ._solid = true,
        ._sunlight = 0,
        ._type = BlockType::LAMP
    };

    const auto litChunk = ChunkLighting::solve_skylight(neighborhood);
    ASSERT_NE(litChunk, nullptr);

    const LocalLight adjacent = litChunk->blocks[lampX + 1][lampY][lampZ]._localLight;
    const LocalLight farther = litChunk->blocks[lampX + 3][lampY][lampZ]._localLight;
    EXPECT_GT(adjacent.r, 0);
    EXPECT_GT(adjacent.r, adjacent.b);
    EXPECT_LT(farther.r, adjacent.r);

    for (int y = 0; y < CHUNK_HEIGHT; ++y)
    {
        for (int z = 0; z < CHUNK_SIZE; ++z)
        {
            center->blocks[lampX + 1][y][z] = Block{
                ._solid = true,
                ._sunlight = 0,
                ._type = BlockType::STONE
            };
        }
    }

    const auto blockedChunk = ChunkLighting::solve_skylight(neighborhood);
    ASSERT_NE(blockedChunk, nullptr);
    const LocalLight blocked = blockedChunk->blocks[lampX + 2][lampY][lampZ]._localLight;
    EXPECT_EQ(blocked.r, 0);
    EXPECT_EQ(blocked.g, 0);
    EXPECT_EQ(blocked.b, 0);
}

TEST(TerrainGeneratorTest, SampleColumnMatchesChunkDataAndIsDeterministic)
{
    TerrainGenerator& generator = TerrainGenerator::instance();

    const TerrainColumnSample first = generator.SampleColumn(128, -64);
    const TerrainColumnSample second = generator.SampleColumn(128, -64);
    const ChunkTerrainData& chunkData = generator.GenerateChunkData(128, -64);
    const TerrainColumnSample fromChunk = chunkData.at(0, 0);

    EXPECT_EQ(first.surfaceHeight, second.surfaceHeight);
    EXPECT_EQ(first.stoneHeight, second.stoneHeight);
    EXPECT_EQ(first.biome, second.biome);
    EXPECT_EQ(first.topBlock, second.topBlock);
    EXPECT_EQ(first.fillerBlock, second.fillerBlock);
    EXPECT_EQ(first.surfaceHeight, fromChunk.surfaceHeight);
    EXPECT_EQ(first.biome, fromChunk.biome);
    EXPECT_GE(first.surfaceHeight, 0);
    EXPECT_LT(first.surfaceHeight, static_cast<int>(CHUNK_HEIGHT));
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
