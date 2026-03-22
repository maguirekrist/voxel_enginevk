#include <memory>
#include <cmath>
#include <limits>
#include <filesystem>
#include <ranges>
#include <fstream>

#include <gtest/gtest.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>

#include "constants.h"
#include "game/block.h"
#include "game/chunk.h"
#include "game/cloud_structure_generator.h"
#include "game/decoration.h"
#include "game/tree_structure_generator.h"
#include "settings/game_settings.h"
#include "config/game_settings_config_repository.h"
#include "config/json_document_store.h"
#include "config/world_gen_config_repository.h"
#include "game/world.h"
#include "render/chunk_decoration_render_registry.h"
#include "voxel/voxel_mesher.h"
#include "voxel/voxel_asset_manager.h"
#include "voxel/voxel_picking.h"
#include "voxel/voxel_render_instance.h"
#include "voxel/voxel_model_repository.h"
#include "world/chunk_lighting.h"
#include "world/chunk_manager.h"
#include "world/terrain_gen.h"

Chunk* ChunkManager::get_chunk(const ChunkCoord) const
{
    return nullptr;
}

namespace
{
    class TestJsonDocumentStore final : public config::IJsonDocumentStore
    {
    public:
        explicit TestJsonDocumentStore(std::filesystem::path rootPath) : _rootPath(std::move(rootPath))
        {
        }

        [[nodiscard]] std::optional<nlohmann::json> load(const std::filesystem::path& path) const override
        {
            const std::filesystem::path resolved = _rootPath / path;
            if (!std::filesystem::exists(resolved))
            {
                return std::nullopt;
            }

            std::ifstream input(resolved);
            nlohmann::json document{};
            input >> document;
            return document;
        }

        void save(const std::filesystem::path& path, const nlohmann::json& document) const override
        {
            const std::filesystem::path resolved = _rootPath / path;
            std::filesystem::create_directories(resolved.parent_path());
            std::ofstream output(resolved, std::ios::trunc);
            output << document.dump(2) << '\n';
        }

    private:
        std::filesystem::path _rootPath{};
    };

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

TEST(VoxelRenderInstanceTest, ModelMatrixAppliesPositionRotationAndScaleToPivotCenteredMesh)
{
    auto asset = std::make_shared<VoxelRuntimeAsset>();
    asset->model.pivot = glm::vec3(1.0f, 0.0f, 0.0f);

    const VoxelRenderInstance instance{
        .asset = asset,
        .position = glm::vec3(10.0f, 4.0f, -2.0f),
        .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)),
        .scale = 2.0f
    };

    const glm::vec3 worldPoint = instance.world_point_from_asset_local(glm::vec3(3.0f, 1.0f, 0.0f));
    EXPECT_NEAR(worldPoint.x, 10.0f, 0.0001f);
    EXPECT_NEAR(worldPoint.y, 6.0f, 0.0001f);
    EXPECT_NEAR(worldPoint.z, -6.0f, 0.0001f);
}

TEST(VoxelRenderInstanceTest, AttachmentWorldTransformUsesAttachmentPositionRelativeToPivot)
{
    auto asset = std::make_shared<VoxelRuntimeAsset>();
    asset->model.pivot = glm::vec3(0.5f, 0.5f, 0.5f);
    asset->model.attachments.push_back(VoxelAttachment{
        .name = "grip",
        .position = glm::vec3(1.5f, 0.5f, 0.5f),
        .forward = glm::vec3(1.0f, 0.0f, 0.0f),
        .up = glm::vec3(0.0f, 1.0f, 0.0f)
    });

    const VoxelRenderInstance instance{
        .asset = asset,
        .position = glm::vec3(3.0f, 2.0f, 1.0f),
        .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)),
        .scale = 1.0f
    };

    const std::optional<glm::mat4> attachmentTransform = instance.attachment_world_transform("grip");
    ASSERT_TRUE(attachmentTransform.has_value());

    const glm::vec3 worldOrigin = glm::vec3(attachmentTransform.value()[3]);
    EXPECT_NEAR(worldOrigin.x, 3.0f, 0.0001f);
    EXPECT_NEAR(worldOrigin.y, 2.0f, 0.0001f);
    EXPECT_NEAR(worldOrigin.z, 0.0f, 0.0001f);
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

TEST(GameSettingsConfigRepositoryTest, SavesAndLoadsSettingsRoundTrip)
{
    settings::GameSettingsPersistence persistence{};
    persistence.world.viewDistance = 14;
    persistence.world.ambientOcclusionEnabled = true;
    persistence.debug.showChunkBoundaries = true;
    persistence.dayNight.paused = true;
    persistence.dayNight.timeOfDay = 0.71f;
    persistence.dayNight.tuning.daySkyZenith = { 0.25f, 0.44f, 0.93f };
    persistence.dayNight.tuning.dayFog = { 0.68f, 0.59f, 0.41f };
    persistence.dayNight.tuning.nightMoon = { 0.31f, 0.37f, 0.72f };
    persistence.dayNight.tuning.aoStrength = 0.22f;
    persistence.dayNight.tuning.shadowStrength = 2.2f;
    persistence.dayNight.tuning.cycleDurationSeconds = 420.0f;

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_game_settings_repo_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const config::GameSettingsConfigRepository repository(documentStore);
    repository.save(persistence);
    const settings::GameSettingsPersistence loaded = repository.load_or_default();
    std::filesystem::remove_all(tempRoot);

    EXPECT_EQ(loaded.world.viewDistance, persistence.world.viewDistance);
    EXPECT_EQ(loaded.world.ambientOcclusionEnabled, persistence.world.ambientOcclusionEnabled);
    EXPECT_EQ(loaded.debug.showChunkBoundaries, persistence.debug.showChunkBoundaries);
    EXPECT_EQ(loaded.dayNight.paused, persistence.dayNight.paused);
    EXPECT_FLOAT_EQ(loaded.dayNight.timeOfDay, persistence.dayNight.timeOfDay);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.daySkyZenith.x, persistence.dayNight.tuning.daySkyZenith.x);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.daySkyZenith.y, persistence.dayNight.tuning.daySkyZenith.y);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.daySkyZenith.z, persistence.dayNight.tuning.daySkyZenith.z);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.dayFog.x, persistence.dayNight.tuning.dayFog.x);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.nightMoon.z, persistence.dayNight.tuning.nightMoon.z);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.aoStrength, persistence.dayNight.tuning.aoStrength);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.shadowStrength, persistence.dayNight.tuning.shadowStrength);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.cycleDurationSeconds, persistence.dayNight.tuning.cycleDurationSeconds);
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

TEST(SettingsManagerTest, ViewDistanceHandlersReceiveDerivedRuntimeValues)
{
    settings::SettingsManager manager;
    settings::ViewDistanceRuntimeSettings last{};
    int notifications = 0;

    manager.bind_view_distance_handler([&](const settings::ViewDistanceRuntimeSettings& runtime)
    {
        last = runtime;
        ++notifications;
    });

    EXPECT_EQ(notifications, 1);
    EXPECT_EQ(last.viewDistance, GameConfig::DEFAULT_VIEW_DISTANCE);
    EXPECT_EQ(last.maximumResidentChunks, maximum_chunks_for_view_distance(GameConfig::DEFAULT_VIEW_DISTANCE));

    const bool changed = manager.mutate([](settings::GameSettingsPersistence& persistence)
    {
        persistence.world.viewDistance = 18;
    });

    EXPECT_TRUE(changed);
    EXPECT_EQ(notifications, 2);
    EXPECT_EQ(last.viewDistance, 18);
    EXPECT_EQ(last.maximumResidentChunks, maximum_chunks_for_view_distance(18));
}

TEST(SettingsManagerTest, AmbientOcclusionUpdatesOnlyRelevantSubscribers)
{
    settings::SettingsManager manager;
    int viewDistanceNotifications = 0;
    int ambientNotifications = 0;
    bool ambientEnabled = false;

    manager.bind_view_distance_handler([&](const settings::ViewDistanceRuntimeSettings&)
    {
        ++viewDistanceNotifications;
    });
    manager.bind_ambient_occlusion_handler([&](const settings::AmbientOcclusionRuntimeSettings& runtime)
    {
        ambientEnabled = runtime.enabled;
        ++ambientNotifications;
    });

    const bool changed = manager.mutate([](settings::GameSettingsPersistence& persistence)
    {
        persistence.world.ambientOcclusionEnabled = true;
    });

    EXPECT_TRUE(changed);
    EXPECT_TRUE(ambientEnabled);
    EXPECT_EQ(ambientNotifications, 2);
    EXPECT_EQ(viewDistanceNotifications, 1);
}

TEST(VoxelModelRepositoryTest, SavesAndLoadsVoxelAssetRoundTrip)
{
    VoxelModel model{};
    model.assetId = "Test Ogre";
    model.displayName = "Test Ogre";
    model.voxelSize = 1.0f / 16.0f;
    model.pivot = glm::vec3(0.5f, 0.0f, 0.5f);
    model.attachments.push_back(VoxelAttachment{
        .name = "right_hand",
        .position = glm::vec3(3.5f, 5.0f, 1.0f),
        .forward = glm::vec3(1.0f, 0.0f, 0.0f),
        .up = glm::vec3(0.0f, 1.0f, 0.0f)
    });
    model.set_voxel(VoxelCoord{ 0, 0, 0 }, VoxelColor{ 255, 0, 0, 255 });
    model.set_voxel(VoxelCoord{ 3, 2, 1 }, VoxelColor{ 12, 34, 56, 255 });

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_model_repo_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const VoxelModelRepository repository(documentStore, "assets");

    repository.save(model);
    const std::optional<VoxelModel> loaded = repository.load("Test Ogre");
    std::filesystem::remove_all(tempRoot);

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->assetId, "testogre");
    EXPECT_EQ(loaded->displayName, "Test Ogre");
    EXPECT_FLOAT_EQ(loaded->voxelSize, model.voxelSize);
    EXPECT_FLOAT_EQ(loaded->pivot.x, model.pivot.x);
    ASSERT_EQ(loaded->attachments.size(), 1u);
    EXPECT_EQ(loaded->attachments[0].name, "right_hand");
    EXPECT_FLOAT_EQ(loaded->attachments[0].position.x, 3.5f);
    EXPECT_FLOAT_EQ(loaded->attachments[0].position.y, 5.0f);
    ASSERT_EQ(loaded->voxel_count(), 2u);

    const VoxelColor* originColor = loaded->try_get(VoxelCoord{ 0, 0, 0 });
    ASSERT_NE(originColor, nullptr);
    EXPECT_EQ(originColor->r, 255);
    EXPECT_EQ(originColor->g, 0);
    EXPECT_EQ(originColor->b, 0);

    const VoxelColor* secondColor = loaded->try_get(VoxelCoord{ 3, 2, 1 });
    ASSERT_NE(secondColor, nullptr);
    EXPECT_EQ(secondColor->r, 12);
    EXPECT_EQ(secondColor->g, 34);
    EXPECT_EQ(secondColor->b, 56);
}

TEST(VoxelAssetManagerTest, LoadsAndCachesRuntimeAssets)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_runtime_asset_manager_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const VoxelModelRepository repository(documentStore, "assets");

    VoxelModel model{};
    model.assetId = "Sword";
    model.displayName = "Sword";
    model.pivot = glm::vec3(0.5f, 0.5f, 0.5f);
    model.attachments.push_back(VoxelAttachment{
        .name = "grip",
        .position = glm::vec3(0.5f, 1.0f, 0.5f),
        .forward = glm::vec3(0.0f, 1.0f, 0.0f),
        .up = glm::vec3(1.0f, 0.0f, 0.0f)
    });
    model.set_voxel(VoxelCoord{ 0, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });
    model.set_voxel(VoxelCoord{ 0, 1, 0 }, VoxelColor{ 255, 255, 255, 255 });
    repository.save(model);

    VoxelAssetManager manager(repository);
    const std::shared_ptr<VoxelRuntimeAsset> firstLoad = manager.load_or_get("Sword");
    const std::shared_ptr<VoxelRuntimeAsset> secondLoad = manager.load_or_get("sword");

    std::filesystem::remove_all(tempRoot);

    ASSERT_NE(firstLoad, nullptr);
    ASSERT_NE(secondLoad, nullptr);
    EXPECT_EQ(firstLoad.get(), secondLoad.get());
    EXPECT_EQ(manager.loaded_asset_count(), 1u);
    EXPECT_EQ(firstLoad->assetId, "sword");
    ASSERT_TRUE(firstLoad->bounds.valid);
    ASSERT_NE(firstLoad->mesh, nullptr);
    EXPECT_FALSE(firstLoad->mesh->_indices.empty());
    ASSERT_EQ(firstLoad->attachments.size(), 1u);
    EXPECT_TRUE(firstLoad->attachments.contains("grip"));
    EXPECT_FLOAT_EQ(firstLoad->attachments.at("grip").position.y, 1.0f);
}

TEST(VoxelMesherTest, GeneratesOnlyExteriorFacesForAdjacentVoxels)
{
    VoxelModel model{};
    model.voxelSize = 1.0f / 16.0f;
    model.set_voxel(VoxelCoord{ 0, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });
    model.set_voxel(VoxelCoord{ 1, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });

    const std::shared_ptr<Mesh> mesh = VoxelMesher::generate_mesh(model);
    ASSERT_NE(mesh, nullptr);

    constexpr uint32_t expectedVisibleFaces = 10;
    EXPECT_EQ(mesh->_vertices.size(), expectedVisibleFaces * 4);
    EXPECT_EQ(mesh->_indices.size(), expectedVisibleFaces * 6);
}

TEST(VoxelPickingTest, FaceFromOutwardNormalMatchesExpectedPlacementFace)
{
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(1, 0, 0)), LEFT_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(-1, 0, 0)), RIGHT_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(0, 1, 0)), TOP_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(0, -1, 0)), BOTTOM_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(0, 0, 1)), FRONT_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(0, 0, -1)), BACK_FACE);
}

TEST(VoxelPickingTest, BuildRayFromCursorPointsForwardAtViewportCenter)
{
    const glm::vec3 cameraPosition{0.0f, 0.0f, 0.0f};
    const glm::vec3 cameraFront = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), 800.0f / 600.0f, 0.1f, 10000.0f);
    projection[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp);

    const voxel::picking::Ray ray = voxel::picking::build_ray_from_cursor(
        399,
        299,
        VkExtent2D{ 800, 600 },
        cameraPosition,
        glm::inverse(projection * view));

    EXPECT_NEAR(ray.direction.x, cameraFront.x, 0.02f);
    EXPECT_NEAR(ray.direction.y, cameraFront.y, 0.02f);
    EXPECT_NEAR(ray.direction.z, cameraFront.z, 0.02f);
}

TEST(VoxelPickingTest, BuildRayFromCursorTopOfViewportPointsUpward)
{
    const glm::vec3 cameraPosition{0.0f, 0.0f, 0.0f};
    const glm::vec3 cameraFront = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), 800.0f / 600.0f, 0.1f, 10000.0f);
    projection[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp);

    const voxel::picking::Ray topRay = voxel::picking::build_ray_from_cursor(
        399,
        0,
        VkExtent2D{ 800, 600 },
        cameraPosition,
        glm::inverse(projection * view));

    const voxel::picking::Ray bottomRay = voxel::picking::build_ray_from_cursor(
        399,
        599,
        VkExtent2D{ 800, 600 },
        cameraPosition,
        glm::inverse(projection * view));

    EXPECT_GT(topRay.direction.y, 0.0f);
    EXPECT_LT(bottomRay.direction.y, 0.0f);
}

TEST(VoxelPickingTest, IntersectRayBoxReturnsCorrectOutwardNormal)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(-2.0f, 0.5f, 0.5f),
        .direction = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 2.0f, 0.0001f);
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(-1, 0, 0));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), RIGHT_FACE);
}

TEST(VoxelPickingTest, IntersectRayBoxReturnsCorrectOutwardNormalForNegativeDirection)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(3.0f, 0.5f, 0.5f),
        .direction = glm::normalize(glm::vec3(-1.0f, 0.0f, 0.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 2.0f, 0.0001f);
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(1, 0, 0));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), LEFT_FACE);
}

TEST(VoxelPickingTest, IntersectRayBoxReturnsCorrectFrontFaceNormalOnZAxis)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(0.5f, 0.5f, -2.0f),
        .direction = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(0, 0, -1));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), BACK_FACE);
}

TEST(VoxelPickingTest, IntersectRayBoxMissesWhenParallelOutsideSlab)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(2.0f, 0.5f, -1.0f),
        .direction = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    EXPECT_FALSE(hit.has_value());
}

TEST(VoxelPickingTest, IntersectRayBoxHitsWhenParallelInsideOtherSlabs)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(0.5f, 0.5f, -2.0f),
        .direction = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 2.0f, 0.0001f);
}

TEST(VoxelPickingTest, IntersectRayBoxRespectsMaximumDistance)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(-2.0f, 0.5f, 0.5f),
        .direction = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        1.5f);

    EXPECT_FALSE(hit.has_value());
}

TEST(VoxelPickingTest, IntersectRayBoxUsesDeterministicNormalForEdgeHits)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(-1.0f, -1.0f, 0.5f),
        .direction = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(-1, 0, 0));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), RIGHT_FACE);
}

TEST(VoxelPickingTest, IntersectRayBoxReturnsExitFaceWhenRayStartsInside)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(0.5f, 0.5f, 0.5f),
        .direction = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 0.5f, 0.0001f);
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(0, 0, 1));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), FRONT_FACE);
}

TEST(VoxelModelRepositoryTest, ListsSavedVoxelAssets)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_model_list_test";
    std::filesystem::remove_all(tempRoot);
    const config::JsonFileDocumentStore documentStore;
    const VoxelModelRepository repository(documentStore, tempRoot / "assets");

    VoxelModel alpha{};
    alpha.assetId = "Alpha";
    alpha.set_voxel(VoxelCoord{ 0, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });
    repository.save(alpha);

    VoxelModel beta{};
    beta.assetId = "beta";
    beta.set_voxel(VoxelCoord{ 1, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });
    repository.save(beta);

    const std::vector<std::string> assetIds = repository.list_asset_ids();
    std::filesystem::remove_all(tempRoot);

    ASSERT_EQ(assetIds.size(), 2u);
    EXPECT_EQ(assetIds[0], "alpha");
    EXPECT_EQ(assetIds[1], "beta");
}
