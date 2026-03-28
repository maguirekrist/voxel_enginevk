#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "test_support.h"
#include "config/world_gen_config_repository.h"
#include "game/chunk.h"
#include "game/world.h"
#include "game/world_collision.h"
#include "world/chunk_lighting.h"
#include "world/dynamic_light_registry.h"
#include "world/terrain_gen.h"
#include "world/world_light_sampler.h"

using test_support::TestJsonDocumentStore;
using test_support::make_empty_chunk;
using test_support::make_empty_neighborhood;

namespace
{
    int count_solid_spans(const ChunkData& chunkData, const int x, const int z)
    {
        bool inSolid = false;
        int spans = 0;
        for (int y = 0; y < static_cast<int>(CHUNK_HEIGHT); ++y)
        {
            const bool solid = chunkData.blocks[x][y][z]._solid;
            if (solid && !inSolid)
            {
                ++spans;
            }
            inSolid = solid;
        }

        return spans;
    }

    bool has_solid_volume_cell(const WorldGenerationChunkResult& generation)
    {
        for (int z = 0; z < static_cast<int>(CHUNK_SIZE); ++z)
        {
            for (int x = 0; x < static_cast<int>(CHUNK_SIZE); ++x)
            {
                for (int y = 0; y < static_cast<int>(CHUNK_HEIGHT); ++y)
                {
                    const TerrainVolumeCell& volumeCell = generation.volumeBuffer.at(x, y, z);
                    if (volumeCell.density <= 0.0f)
                    {
                        continue;
                    }

                    return true;
                }
            }
        }

        return false;
    }

    bool appearance_buffer_is_empty(const AppearanceBuffer& appearanceBuffer)
    {
        for (int z = 0; z < static_cast<int>(CHUNK_SIZE); ++z)
        {
            for (int x = 0; x < static_cast<int>(CHUNK_SIZE); ++x)
            {
                for (int y = 0; y < static_cast<int>(CHUNK_HEIGHT); ++y)
                {
                    if (appearanceBuffer.packed_color(x, y, z) != 0u)
                    {
                        return false;
                    }
                }
            }
        }

        return true;
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
    EXPECT_EQ(first.topBlock, BlockType::STONE);
    EXPECT_EQ(first.fillerBlock, BlockType::STONE);
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
    updatedSettings.continentalSplines.back().heightValue = std::min(updatedSettings.continentalSplines.back().heightValue + 12.0f, static_cast<float>(CHUNK_HEIGHT - 1));
    generator.apply_settings(updatedSettings);

    const TerrainColumnSample changedA = generator.SampleColumn(128, -64);
    const TerrainColumnSample changedB = generator.SampleColumn(144, -48);

    generator.apply_settings(originalSettings);

    const bool anyChanged =
        baselineA.surfaceHeight != changedA.surfaceHeight ||
        baselineA.noise.continentalness != changedA.noise.continentalness ||
        baselineB.surfaceHeight != changedB.surfaceHeight ||
        baselineB.noise.river != changedB.noise.river;

    EXPECT_TRUE(anyChanged);
}

TEST(TerrainGeneratorTest, RiversCanBeDisabled)
{
    TerrainGenerator& generator = TerrainGenerator::instance();
    const TerrainGeneratorSettings originalSettings = generator.settings();

    TerrainGeneratorSettings enabledSettings = originalSettings;
    enabledSettings.shape.riversEnabled = true;

    TerrainGeneratorSettings disabledSettings = enabledSettings;
    disabledSettings.shape.riversEnabled = false;

    auto count_river_columns = [&generator](const TerrainGeneratorSettings& settings)
    {
        generator.apply_settings(settings);

        int riverColumns = 0;
        for (int chunkZ = -6; chunkZ <= 6; ++chunkZ)
        {
            for (int chunkX = -6; chunkX <= 6; ++chunkX)
            {
                const ChunkTerrainData chunkData = generator.GenerateChunkData(
                    chunkX * static_cast<int>(CHUNK_SIZE),
                    chunkZ * static_cast<int>(CHUNK_SIZE));
                for (int z = 0; z < static_cast<int>(CHUNK_SIZE); ++z)
                {
                    for (int x = 0; x < static_cast<int>(CHUNK_SIZE); ++x)
                    {
                        if (chunkData.at(x, z).hasRiver)
                        {
                            ++riverColumns;
                        }
                    }
                }
            }
        }

        return riverColumns;
    };

    const int enabledRiverColumns = count_river_columns(enabledSettings);
    const int disabledRiverColumns = count_river_columns(disabledSettings);

    generator.apply_settings(originalSettings);

    EXPECT_GT(enabledRiverColumns, 0);
    EXPECT_EQ(disabledRiverColumns, 0);
}

TEST(TerrainGeneratorTest, ChunkPipelineBuildsConsistentHeightfieldProducts)
{
    TerrainGenerator& generator = TerrainGenerator::instance();
    constexpr int chunkOriginX = 128;
    constexpr int chunkOriginZ = -64;

    const WorldGenerationChunkResult generation = generator.GenerateChunkPipeline(chunkOriginX, chunkOriginZ);
    const TerrainColumnSample sampled = generator.SampleColumn(chunkOriginX, chunkOriginZ);
    const TerrainColumnSample pipelineColumn = generation.columnScaffold.at(0, 0);

    EXPECT_EQ(generation.regionScaffold.chunkOrigin.x, chunkOriginX);
    EXPECT_EQ(generation.regionScaffold.chunkOrigin.y, chunkOriginZ);
    EXPECT_EQ(generation.columnScaffold.chunkOrigin.x, chunkOriginX);
    EXPECT_EQ(generation.columnScaffold.chunkOrigin.y, chunkOriginZ);
    EXPECT_EQ(generation.featureInstances.chunkOrigin.x, chunkOriginX);
    EXPECT_EQ(generation.featureInstances.chunkOrigin.y, chunkOriginZ);
    EXPECT_EQ(generation.volumeBuffer.chunkOrigin.x, chunkOriginX);
    EXPECT_EQ(generation.volumeBuffer.chunkOrigin.y, chunkOriginZ);
    EXPECT_EQ(generation.surfaceClassification.chunkOrigin.x, chunkOriginX);
    EXPECT_EQ(generation.surfaceClassification.chunkOrigin.y, chunkOriginZ);
    EXPECT_EQ(generation.appearanceBuffer.chunkOrigin.x, chunkOriginX);
    EXPECT_EQ(generation.appearanceBuffer.chunkOrigin.y, chunkOriginZ);

    EXPECT_EQ(
        generation.regionScaffold.cells.size(),
        static_cast<size_t>(CHUNK_SIZE * CHUNK_SIZE));
    EXPECT_EQ(
        generation.columnScaffold.columns.size(),
        static_cast<size_t>(CHUNK_SIZE * CHUNK_SIZE));
    EXPECT_EQ(
        generation.volumeBuffer.cells.size(),
        static_cast<size_t>(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE));
    EXPECT_EQ(
        generation.surfaceClassification.faces.size(),
        static_cast<size_t>(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE));
    EXPECT_EQ(
        generation.appearanceBuffer.voxels.size(),
        static_cast<size_t>(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE));
    EXPECT_TRUE(generation.featureInstances.features.empty());

    EXPECT_EQ(sampled.surfaceHeight, pipelineColumn.surfaceHeight);
    EXPECT_EQ(sampled.stoneHeight, pipelineColumn.stoneHeight);
    EXPECT_EQ(sampled.biome, pipelineColumn.biome);
    EXPECT_EQ(sampled.topBlock, pipelineColumn.topBlock);
    EXPECT_EQ(sampled.fillerBlock, pipelineColumn.fillerBlock);
    EXPECT_TRUE(has_solid_volume_cell(generation));
    EXPECT_TRUE(appearance_buffer_is_empty(generation.appearanceBuffer));
}

TEST(TerrainGeneratorTest, SeaLevelControlsAirWaterFillHeight)
{
    TerrainGenerator& generator = TerrainGenerator::instance();
    const TerrainGeneratorSettings originalSettings = generator.settings();

    TerrainGeneratorSettings settings = originalSettings;
    settings.shape.seaLevel = 74;
    generator.apply_settings(settings);

    ChunkTerrainData terrainData{
        .chunkOrigin = {0, 0},
        .columns = std::vector<TerrainColumnSample>(
            static_cast<size_t>(CHUNK_SIZE * CHUNK_SIZE))
    };
    for (TerrainColumnSample& column : terrainData.columns)
    {
        column.surfaceHeight = 10;
        column.topBlock = BlockType::STONE;
        column.fillerBlock = BlockType::STONE;
        column.biome = BiomeType::None;
    }

    ChunkData chunkData{};
    generator.PopulateBaseTerrainBlocks(terrainData, chunkData);
    generator.apply_settings(originalSettings);

    EXPECT_EQ(chunkData.blocks[0][74][0]._type, BlockType::WATER);
    EXPECT_EQ(chunkData.blocks[0][75][0]._type, BlockType::AIR);
}

TEST(TerrainGeneratorTest, HeightfieldTerrainRasterizesAsStone)
{
    TerrainGenerator& generator = TerrainGenerator::instance();
    const TerrainGeneratorSettings originalSettings = generator.settings();

    TerrainGeneratorSettings settings = originalSettings;
    generator.apply_settings(settings);

    bool foundStoneColumn = false;
    for (int chunkZ = -6; chunkZ <= 6 && !foundStoneColumn; ++chunkZ)
    {
        for (int chunkX = -6; chunkX <= 6 && !foundStoneColumn; ++chunkX)
        {
            ChunkData chunkData{
                .coord = ChunkCoord{chunkX, chunkZ},
                .position = glm::ivec2(chunkX * static_cast<int>(CHUNK_SIZE), chunkZ * static_cast<int>(CHUNK_SIZE))
            };
            const WorldGenerationChunkResult generation = generator.GenerateChunkPipeline(chunkData.position.x, chunkData.position.y);
            generator.RasterizeChunkTerrain(generation, chunkData);

            for (int z = 0; z < static_cast<int>(CHUNK_SIZE) && !foundStoneColumn; ++z)
            {
                for (int x = 0; x < static_cast<int>(CHUNK_SIZE) && !foundStoneColumn; ++x)
                {
                    const TerrainColumnSample& column = generation.columnScaffold.at(x, z);
                    if (column.surfaceHeight < 1)
                    {
                        continue;
                    }

                    foundStoneColumn = true;
                    EXPECT_EQ(chunkData.blocks[x][column.surfaceHeight][z]._type, BlockType::STONE);
                    EXPECT_EQ(chunkData.blocks[x][column.surfaceHeight - 1][z]._type, BlockType::STONE);
                    EXPECT_EQ(generation.volumeBuffer.at(x, column.surfaceHeight - 1, z).material, MaterialClass::Stone);
                    EXPECT_TRUE(appearance_buffer_is_empty(generation.appearanceBuffer));
                }
            }
        }
    }

    generator.apply_settings(originalSettings);

    EXPECT_TRUE(foundStoneColumn);
}

TEST(TerrainGeneratorTest, ChunkGenerationKeepsDefaultTerrainAppearance)
{
    ChunkData chunkData{
        .coord = ChunkCoord{8, -4},
        .position = glm::ivec2(128, -64)
    };

    chunkData.generate();

    ASSERT_NE(chunkData.terrainAppearance, nullptr);
    EXPECT_TRUE(appearance_buffer_is_empty(*chunkData.terrainAppearance));
}

TEST(WorldGenConfigRepositoryTest, SavesAndLoadsSettingsRoundTrip)
{
    TerrainGeneratorSettings settings = TerrainGenerator::default_settings();
    settings.seed = 987654321u;
    settings.shape.continentalFrequency = 0.0022f;
    settings.shape.seaLevel = 71;
    settings.shape.riversEnabled = false;
    settings.shape.peaksFrequency = 0.0031f;
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
    EXPECT_EQ(loaded.shape.seaLevel, settings.shape.seaLevel);
    EXPECT_EQ(loaded.shape.riversEnabled, settings.shape.riversEnabled);
    EXPECT_FLOAT_EQ(loaded.shape.peaksFrequency, settings.shape.peaksFrequency);
    ASSERT_GT(loaded.peakSplines.size(), 1);
    EXPECT_FLOAT_EQ(loaded.peakSplines[1].heightValue, settings.peakSplines[1].heightValue);
}
