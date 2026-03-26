#include <array>
#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "test_support.h"
#include "config/world_gen_config_repository.h"
#include "game/world.h"
#include "game/world_collision.h"
#include "world/chunk_lighting.h"
#include "world/dynamic_light_registry.h"
#include "world/terrain_gen.h"
#include "world/world_light_sampler.h"

using test_support::TestJsonDocumentStore;
using test_support::make_empty_chunk;
using test_support::make_empty_neighborhood;

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

    const glm::ivec3 negativeY = World::get_local_coordinates(glm::vec3(2.0f, -1.0f, 2.0f));
    EXPECT_EQ(negativeY.y, -1);
}

TEST(WorldCollisionTest, IntersectsSolidBlocksEnumeratesOverlappingVoxelRange)
{
    const AABB bounds{
        .min = glm::vec3(1.2f, 3.0f, 4.2f),
        .max = glm::vec3(1.8f, 4.7f, 4.8f)
    };

    EXPECT_TRUE(WorldCollision::intersects_solid_blocks(bounds, [](const glm::ivec3& blockPos) -> bool
    {
        return blockPos == glm::ivec3(1, 3, 4);
    }));

    EXPECT_FALSE(WorldCollision::intersects_solid_blocks(bounds, [](const glm::ivec3& blockPos) -> bool
    {
        return blockPos == glm::ivec3(2, 3, 4);
    }));
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

TEST(WorldLightSamplerTest, SamplesBakedLightFromLitChunkData)
{
    ChunkData chunk{
        .coord = ChunkCoord{0, 0},
        .position = glm::ivec2(0, 0)
    };

    chunk.blocks[3][9][4] = Block{
        ._solid = false,
        ._sunlight = 12,
        ._type = BlockType::AIR,
        ._localLight = LocalLight{ .r = 6, .g = 3, .b = 0 }
    };

    const world_lighting::SampledWorldLight sampled = world_lighting::sample_baked_world_light(
        chunk,
        glm::vec3(3.2f, 9.6f, 4.4f));

    EXPECT_NEAR(sampled.bakedSunlight, 12.0f / static_cast<float>(MAX_LIGHT_LEVEL), 0.0001f);
    EXPECT_NEAR(sampled.bakedLocalLight.r, 6.0f / static_cast<float>(MAX_LIGHT_LEVEL), 0.0001f);
    EXPECT_NEAR(sampled.bakedLocalLight.g, 3.0f / static_cast<float>(MAX_LIGHT_LEVEL), 0.0001f);
    EXPECT_NEAR(sampled.bakedLocalLight.b, 0.0f, 0.0001f);
}

TEST(WorldLightSamplerTest, SamplesDynamicLightsWithAffectMaskFiltering)
{
    const std::array lights{
        world_lighting::DynamicPointLight{
            .id = 1,
            .position = glm::vec3(0.0f, 0.0f, 0.0f),
            .color = glm::vec3(1.0f, 0.5f, 0.25f),
            .intensity = 1.0f,
            .radius = 4.0f,
            .affectMask = world_lighting::AffectProps,
            .active = true
        },
        world_lighting::DynamicPointLight{
            .id = 2,
            .position = glm::vec3(10.0f, 0.0f, 0.0f),
            .color = glm::vec3(0.0f, 1.0f, 0.0f),
            .intensity = 3.0f,
            .radius = 2.0f,
            .affectMask = world_lighting::AffectWorld,
            .active = true
        }
    };

    const glm::vec3 sampledProps = world_lighting::sample_dynamic_point_lights(
        lights,
        glm::vec3(1.0f, 0.0f, 0.0f),
        world_lighting::AffectProps);
    EXPECT_GT(sampledProps.r, 0.0f);
    EXPECT_GT(sampledProps.g, 0.0f);
    EXPECT_GT(sampledProps.b, 0.0f);

    const glm::vec3 sampledWorld = world_lighting::sample_dynamic_point_lights(
        lights,
        glm::vec3(1.0f, 0.0f, 0.0f),
        world_lighting::AffectWorld);
    EXPECT_NEAR(sampledWorld.r, 0.0f, 0.0001f);
    EXPECT_NEAR(sampledWorld.g, 0.0f, 0.0001f);
    EXPECT_NEAR(sampledWorld.b, 0.0f, 0.0001f);
}

TEST(TerrainGeneratorTest, SampleColumnMatchesChunkDataAndIsDeterministic)
{
    TerrainGenerator& generator = TerrainGenerator::instance();

    const TerrainColumnSample first = generator.SampleColumn(128, -64);
    const TerrainColumnSample second = generator.SampleColumn(128, -64);
    const ChunkTerrainData chunkData = generator.GenerateChunkData(128, -64);
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

TEST(TerrainGeneratorTest, ApplyingSettingsChangesGeneratedTerrain)
{
    TerrainGenerator& generator = TerrainGenerator::instance();
    const TerrainGeneratorSettings originalSettings = generator.settings();
    const TerrainColumnSample baselineA = generator.SampleColumn(128, -64);
    const TerrainColumnSample baselineB = generator.SampleColumn(144, -48);

    TerrainGeneratorSettings updatedSettings = originalSettings;
    updatedSettings.seed += 97;
    updatedSettings.shape.continentalFrequency *= 1.35f;
    updatedSettings.shape.peaksFrequency *= 1.45f;
    updatedSettings.shape.detailStrength += 3.0f;
    updatedSettings.shape.riverFrequency *= 0.75f;
    updatedSettings.biome.mountainHeightOffset += 12;
    updatedSettings.continentalSplines.back().heightValue = std::min(updatedSettings.continentalSplines.back().heightValue + 12.0f, static_cast<float>(CHUNK_HEIGHT - 1));
    generator.apply_settings(updatedSettings);

    const TerrainColumnSample changedA = generator.SampleColumn(128, -64);
    const TerrainColumnSample changedB = generator.SampleColumn(144, -48);

    generator.apply_settings(originalSettings);

    const bool anyChanged =
        baselineA.surfaceHeight != changedA.surfaceHeight ||
        baselineA.biome != changedA.biome ||
        baselineA.noise.continentalness != changedA.noise.continentalness ||
        baselineB.surfaceHeight != changedB.surfaceHeight ||
        baselineB.biome != changedB.biome ||
        baselineB.noise.river != changedB.noise.river;

    EXPECT_TRUE(anyChanged);
}

TEST(WorldGenConfigRepositoryTest, SavesAndLoadsSettingsRoundTrip)
{
    TerrainGeneratorSettings settings = TerrainGenerator::default_settings();
    settings.seed = 987654321u;
    settings.shape.continentalFrequency = 0.0022f;
    settings.shape.peaksFrequency = 0.0031f;
    settings.biome.mountainHeightOffset = 63;
    settings.surface.riverMaxDepth = 7;
    settings.peakSplines[1].heightValue = 17.0f;

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_config_repo_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const config::WorldGenConfigRepository repository(documentStore);
    repository.save(settings);
    const TerrainGeneratorSettings loaded = repository.load_or_default();
    std::filesystem::remove_all(tempRoot);

    EXPECT_EQ(loaded.seed, settings.seed);
    EXPECT_FLOAT_EQ(loaded.shape.continentalFrequency, settings.shape.continentalFrequency);
    EXPECT_FLOAT_EQ(loaded.shape.peaksFrequency, settings.shape.peaksFrequency);
    EXPECT_EQ(loaded.biome.mountainHeightOffset, settings.biome.mountainHeightOffset);
    EXPECT_EQ(loaded.surface.riverMaxDepth, settings.surface.riverMaxDepth);
    ASSERT_GT(loaded.peakSplines.size(), 1);
    EXPECT_FLOAT_EQ(loaded.peakSplines[1].heightValue, settings.peakSplines[1].heightValue);
}
