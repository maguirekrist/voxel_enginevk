#include <memory>
#include <limits>
#include <filesystem>
#include <ranges>
#include <fstream>

#include <gtest/gtest.h>

#include "constants.h"
#include "game/block.h"
#include "game/chunk.h"
#include "game/tree_structure_generator.h"
#include "settings/game_settings.h"
#include "config/json_document_store.h"
#include "config/world_gen_config_repository.h"
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
    updatedSettings.shape.terrainFrequency *= 1.6f;
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
    settings.shape.terrainFrequency = 0.0022f;
    settings.biome.mountainHeightOffset = 63;
    settings.surface.riverMaxDepth = 7;
    settings.peakSplines[1].heightValue = 17.0f;

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

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_config_repo_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const config::WorldGenConfigRepository repository(documentStore);
    repository.save(settings);
    const TerrainGeneratorSettings loaded = repository.load_or_default();
    std::filesystem::remove_all(tempRoot);

    EXPECT_EQ(loaded.seed, settings.seed);
    EXPECT_FLOAT_EQ(loaded.shape.terrainFrequency, settings.shape.terrainFrequency);
    EXPECT_EQ(loaded.biome.mountainHeightOffset, settings.biome.mountainHeightOffset);
    EXPECT_EQ(loaded.surface.riverMaxDepth, settings.surface.riverMaxDepth);
    ASSERT_GT(loaded.peakSplines.size(), 1);
    EXPECT_FLOAT_EQ(loaded.peakSplines[1].heightValue, settings.peakSplines[1].heightValue);
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
