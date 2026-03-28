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

    bool volume_matches_surface_height(const WorldGenerationChunkResult& generation)
    {
        for (int z = 0; z < static_cast<int>(CHUNK_SIZE); ++z)
        {
            for (int x = 0; x < static_cast<int>(CHUNK_SIZE); ++x)
            {
                const TerrainColumnSample& column = generation.columnScaffold.at(x, z);
                for (int y = 0; y < static_cast<int>(CHUNK_HEIGHT); ++y)
                {
                    const bool expectedSolid = y <= column.surfaceHeight;
                    const bool actualSolid = generation.volumeBuffer.at(x, y, z).density > 0.0f;
                    if (expectedSolid != actualSolid)
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
    updatedSettings.shape.continental.frequency *= 1.35f;
    updatedSettings.shape.peaks.frequency *= 1.45f;
    updatedSettings.shape.erosion.strength *= 0.8f;
    updatedSettings.shape.peaks.octaves = std::min(updatedSettings.shape.peaks.octaves + 2, 16);
    updatedSettings.shape.erosion.terraceStepCount = std::min(updatedSettings.shape.erosion.terraceStepCount + 5, 32);
    updatedSettings.continentalSplines.back().heightValue = std::min(updatedSettings.continentalSplines.back().heightValue + 12.0f, static_cast<float>(CHUNK_HEIGHT - 1));
    generator.apply_settings(updatedSettings);

    const TerrainColumnSample changedA = generator.SampleColumn(128, -64);
    const TerrainColumnSample changedB = generator.SampleColumn(144, -48);

    generator.apply_settings(originalSettings);

    const bool anyChanged =
        baselineA.surfaceHeight != changedA.surfaceHeight ||
        baselineA.noise.continentalness != changedA.noise.continentalness ||
        baselineB.surfaceHeight != changedB.surfaceHeight ||
        baselineB.noise.peaksValleys != changedB.noise.peaksValleys;

    EXPECT_TRUE(anyChanged);
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

TEST(TerrainGeneratorTest, WeirdnessDisabledPreservesExactHeightfieldVolume)
{
    TerrainGenerator& generator = TerrainGenerator::instance();
    const TerrainGeneratorSettings originalSettings = generator.settings();

    TerrainGeneratorSettings settings = originalSettings;
    settings.shape.weirdness.strength = 0.0f;
    settings.density.strength = 1.0f;
    settings.density.maxBandHalfSpanBlocks = static_cast<int>(CHUNK_HEIGHT);
    generator.apply_settings(settings);

    const WorldGenerationChunkResult generation = generator.GenerateChunkPipeline(128, -64);

    generator.apply_settings(originalSettings);

    EXPECT_TRUE(volume_matches_surface_height(generation));
}

TEST(TerrainGeneratorTest, HighWeirdnessCanCreateMultiSpanColumns)
{
    TerrainGenerator& generator = TerrainGenerator::instance();
    const TerrainGeneratorSettings originalSettings = generator.settings();

    TerrainGeneratorSettings settings = originalSettings;
    settings.seed = 424242u;
    settings.shape.weirdness.frequency = 0.00005f;
    settings.shape.weirdness.octaves = 1;
    settings.shape.weirdness.terraceStepCount = 2;
    settings.shape.weirdness.terraceSmoothness = 0.0f;
    settings.shape.weirdness.strength = 1.0f;
    settings.density.frequency = 0.0035f;
    settings.density.octaves = 5;
    settings.density.gain = 0.55f;
    settings.density.strength = 1.0f;
    settings.density.maxBandHalfSpanBlocks = static_cast<int>(CHUNK_HEIGHT);
    generator.apply_settings(settings);

    bool foundMultiSpanColumn = false;
    for (int chunkZ = -2; chunkZ <= 2 && !foundMultiSpanColumn; ++chunkZ)
    {
        for (int chunkX = -2; chunkX <= 2 && !foundMultiSpanColumn; ++chunkX)
        {
            ChunkData chunkData{
                .coord = ChunkCoord{chunkX, chunkZ},
                .position = glm::ivec2(chunkX * static_cast<int>(CHUNK_SIZE), chunkZ * static_cast<int>(CHUNK_SIZE))
            };
            const WorldGenerationChunkResult generation = generator.GenerateChunkPipeline(chunkData.position.x, chunkData.position.y);
            generator.RasterizeChunkTerrain(generation, chunkData);

            for (int z = 0; z < static_cast<int>(CHUNK_SIZE) && !foundMultiSpanColumn; ++z)
            {
                for (int x = 0; x < static_cast<int>(CHUNK_SIZE) && !foundMultiSpanColumn; ++x)
                {
                    foundMultiSpanColumn = count_solid_spans(chunkData, x, z) > 1;
                }
            }
        }
    }

    generator.apply_settings(originalSettings);

    EXPECT_TRUE(foundMultiSpanColumn);
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

TEST(TerrainGeneratorTest, VolumetricTerrainRasterizesStoneSolids)
{
    TerrainGenerator& generator = TerrainGenerator::instance();
    const TerrainGeneratorSettings originalSettings = generator.settings();

    TerrainGeneratorSettings settings = originalSettings;
    generator.apply_settings(settings);

    bool foundSolidStoneVoxel = false;
    for (int chunkZ = -6; chunkZ <= 6 && !foundSolidStoneVoxel; ++chunkZ)
    {
        for (int chunkX = -6; chunkX <= 6 && !foundSolidStoneVoxel; ++chunkX)
        {
            ChunkData chunkData{
                .coord = ChunkCoord{chunkX, chunkZ},
                .position = glm::ivec2(chunkX * static_cast<int>(CHUNK_SIZE), chunkZ * static_cast<int>(CHUNK_SIZE))
            };
            const WorldGenerationChunkResult generation = generator.GenerateChunkPipeline(chunkData.position.x, chunkData.position.y);
            generator.RasterizeChunkTerrain(generation, chunkData);

            for (int z = 0; z < static_cast<int>(CHUNK_SIZE) && !foundSolidStoneVoxel; ++z)
            {
                for (int x = 0; x < static_cast<int>(CHUNK_SIZE) && !foundSolidStoneVoxel; ++x)
                {
                    for (int y = 0; y < static_cast<int>(CHUNK_HEIGHT) && !foundSolidStoneVoxel; ++y)
                    {
                        const TerrainVolumeCell& cell = generation.volumeBuffer.at(x, y, z);
                        if (cell.density <= 0.0f || cell.material != MaterialClass::Stone)
                        {
                            continue;
                        }

                        foundSolidStoneVoxel = true;
                        EXPECT_TRUE(chunkData.blocks[x][y][z]._solid);
                        EXPECT_EQ(chunkData.blocks[x][y][z]._type, BlockType::STONE);
                        EXPECT_TRUE(appearance_buffer_is_empty(generation.appearanceBuffer));
                    }
                }
            }
        }
    }

    generator.apply_settings(originalSettings);

    EXPECT_TRUE(foundSolidStoneVoxel);
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
    settings.shape.continental.frequency = 0.0022f;
    settings.shape.continental.octaves = 6;
    settings.shape.continental.lacunarity = 2.4f;
    settings.shape.continental.gain = 0.42f;
    settings.shape.continental.weightedStrength = 0.35f;
    settings.shape.continental.remapFromMin = -0.45f;
    settings.shape.continental.remapFromMax = 0.55f;
    settings.shape.continental.remapToMin = -1.0f;
    settings.shape.continental.remapToMax = 1.0f;
    settings.shape.continental.terraceStepCount = 6;
    settings.shape.continental.terraceSmoothness = 0.25f;
    settings.shape.continental.strength = 0.8f;
    settings.shape.weirdness.frequency = 0.0019f;
    settings.shape.weirdness.remapFromMin = -0.25f;
    settings.shape.weirdness.remapFromMax = 0.25f;
    settings.shape.weirdness.remapToMin = -1.0f;
    settings.shape.weirdness.remapToMax = 1.0f;
    settings.shape.weirdness.terraceStepCount = 5;
    settings.shape.weirdness.terraceSmoothness = 0.3f;
    settings.shape.seaLevel = 71;
    settings.shape.peaks.frequency = 0.0031f;
    settings.shape.peaks.basis = TerrainNoiseBasis::Perlin;
    settings.density.basis = TerrainNoiseBasis::Simplex;
    settings.density.frequency = 0.0028f;
    settings.density.octaves = 6;
    settings.density.lacunarity = 2.2f;
    settings.density.gain = 0.45f;
    settings.density.weightedStrength = 0.2f;
    settings.density.strength = 0.9f;
    settings.density.maxBandHalfSpanBlocks = 44;
    settings.peakSplines[1].heightValue = 17.0f;

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_config_repo_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const config::WorldGenConfigRepository repository(documentStore);
    repository.save(settings);
    const TerrainGeneratorSettings loaded = repository.load_or_default();
    std::filesystem::remove_all(tempRoot);

    EXPECT_EQ(loaded.seed, settings.seed);
    EXPECT_EQ(loaded.shape.continental.basis, settings.shape.continental.basis);
    EXPECT_FLOAT_EQ(loaded.shape.continental.frequency, settings.shape.continental.frequency);
    EXPECT_EQ(loaded.shape.continental.octaves, settings.shape.continental.octaves);
    EXPECT_FLOAT_EQ(loaded.shape.continental.lacunarity, settings.shape.continental.lacunarity);
    EXPECT_FLOAT_EQ(loaded.shape.continental.gain, settings.shape.continental.gain);
    EXPECT_FLOAT_EQ(loaded.shape.continental.weightedStrength, settings.shape.continental.weightedStrength);
    EXPECT_FLOAT_EQ(loaded.shape.continental.remapFromMin, settings.shape.continental.remapFromMin);
    EXPECT_FLOAT_EQ(loaded.shape.continental.remapFromMax, settings.shape.continental.remapFromMax);
    EXPECT_FLOAT_EQ(loaded.shape.continental.remapToMin, settings.shape.continental.remapToMin);
    EXPECT_FLOAT_EQ(loaded.shape.continental.remapToMax, settings.shape.continental.remapToMax);
    EXPECT_EQ(loaded.shape.continental.terraceStepCount, settings.shape.continental.terraceStepCount);
    EXPECT_FLOAT_EQ(loaded.shape.continental.terraceSmoothness, settings.shape.continental.terraceSmoothness);
    EXPECT_FLOAT_EQ(loaded.shape.continental.strength, settings.shape.continental.strength);
    EXPECT_FLOAT_EQ(loaded.shape.weirdness.frequency, settings.shape.weirdness.frequency);
    EXPECT_FLOAT_EQ(loaded.shape.weirdness.remapFromMin, settings.shape.weirdness.remapFromMin);
    EXPECT_FLOAT_EQ(loaded.shape.weirdness.remapFromMax, settings.shape.weirdness.remapFromMax);
    EXPECT_FLOAT_EQ(loaded.shape.weirdness.remapToMin, settings.shape.weirdness.remapToMin);
    EXPECT_FLOAT_EQ(loaded.shape.weirdness.remapToMax, settings.shape.weirdness.remapToMax);
    EXPECT_EQ(loaded.shape.weirdness.terraceStepCount, settings.shape.weirdness.terraceStepCount);
    EXPECT_FLOAT_EQ(loaded.shape.weirdness.terraceSmoothness, settings.shape.weirdness.terraceSmoothness);
    EXPECT_EQ(loaded.shape.seaLevel, settings.shape.seaLevel);
    EXPECT_EQ(loaded.shape.peaks.basis, settings.shape.peaks.basis);
    EXPECT_FLOAT_EQ(loaded.shape.peaks.frequency, settings.shape.peaks.frequency);
    EXPECT_EQ(loaded.density.basis, settings.density.basis);
    EXPECT_FLOAT_EQ(loaded.density.frequency, settings.density.frequency);
    EXPECT_EQ(loaded.density.octaves, settings.density.octaves);
    EXPECT_FLOAT_EQ(loaded.density.lacunarity, settings.density.lacunarity);
    EXPECT_FLOAT_EQ(loaded.density.gain, settings.density.gain);
    EXPECT_FLOAT_EQ(loaded.density.weightedStrength, settings.density.weightedStrength);
    EXPECT_FLOAT_EQ(loaded.density.strength, settings.density.strength);
    EXPECT_EQ(loaded.density.maxBandHalfSpanBlocks, settings.density.maxBandHalfSpanBlocks);
    ASSERT_GT(loaded.peakSplines.size(), 1);
    EXPECT_FLOAT_EQ(loaded.peakSplines[1].heightValue, settings.peakSplines[1].heightValue);
}
