#include <filesystem>

#include <gtest/gtest.h>

#include "config/world_geometry_config_repository.h"
#include "test_support.h"
#include "world/world_geometry.h"

using test_support::TestJsonDocumentStore;

TEST(WorldGeometryTest, DefaultsMatchCurrentChunkGeometry)
{
    const WorldGeometry geometry(default_world_geometry_settings());

    EXPECT_EQ(geometry.chunk_voxel_width(), 16);
    EXPECT_EQ(geometry.chunk_voxel_height(), 256);
    EXPECT_FLOAT_EQ(geometry.chunk_world_width(), 16.0f);
    EXPECT_FLOAT_EQ(geometry.chunk_world_height(), 256.0f);
    EXPECT_FLOAT_EQ(geometry.block_world_size(), 1.0f);
}

TEST(WorldGeometryTest, NormalizationPreservesCubicBlocksByAdjustingVoxelHeight)
{
    WorldGeometrySettings settings{};
    settings.chunkVoxelWidth = 24;
    settings.chunkVoxelHeight = 256;
    settings.chunkWorldWidth = 16.0f;
    settings.chunkWorldHeight = 256.0f;

    normalize_world_geometry_settings(settings);

    EXPECT_EQ(settings.chunkVoxelWidth, 24);
    EXPECT_EQ(settings.chunkVoxelHeight, 384);
    EXPECT_FLOAT_EQ(settings.chunkWorldWidth, 16.0f);
    EXPECT_FLOAT_EQ(settings.chunkWorldHeight, 256.0f);

    const WorldGeometry geometry(settings);
    EXPECT_NEAR(geometry.block_world_size(), 2.0f / 3.0f, 0.0001f);
}

TEST(WorldGeometryTest, WorldCoordinateConversionUsesPhysicalChunkExtent)
{
    WorldGeometrySettings settings{};
    settings.chunkVoxelWidth = 24;
    settings.chunkWorldWidth = 16.0f;
    settings.chunkWorldHeight = 256.0f;

    const WorldGeometry geometry(settings);

    EXPECT_EQ(geometry.world_to_chunk(glm::vec3(0.0f, 0.0f, 0.0f)), (ChunkCoord{0, 0}));
    EXPECT_EQ(geometry.world_to_chunk(glm::vec3(15.9f, 10.0f, 15.9f)), (ChunkCoord{0, 0}));
    EXPECT_EQ(geometry.world_to_chunk(glm::vec3(16.0f, 10.0f, 16.0f)), (ChunkCoord{1, 1}));
    EXPECT_EQ(geometry.world_to_chunk(glm::vec3(-0.1f, 10.0f, -0.1f)), (ChunkCoord{-1, -1}));

    const glm::ivec3 local = geometry.world_to_local_voxel(glm::vec3(16.0f / 3.0f, 4.0f, 16.0f / 3.0f));
    EXPECT_EQ(local.x, 8);
    EXPECT_EQ(local.y, 6);
    EXPECT_EQ(local.z, 8);

    const glm::ivec3 wrappedNegative = geometry.world_to_local_voxel(glm::vec3(-0.1f, 0.0f, -0.1f));
    EXPECT_EQ(wrappedNegative.x, geometry.chunk_voxel_width() - 1);
    EXPECT_EQ(wrappedNegative.z, geometry.chunk_voxel_width() - 1);
}

TEST(WorldGeometryConfigRepositoryTest, SavesAndLoadsNormalizedGeometrySettingsRoundTrip)
{
    WorldGeometrySettings settings{};
    settings.chunkVoxelWidth = 24;
    settings.chunkVoxelHeight = 300;
    settings.chunkWorldWidth = 16.0f;
    settings.chunkWorldHeight = 256.0f;

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_world_geometry_repo_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const config::WorldGeometryConfigRepository repository(documentStore);
    repository.save(settings);
    const WorldGeometrySettings loaded = repository.load_or_default();
    std::filesystem::remove_all(tempRoot);

    EXPECT_EQ(loaded.chunkVoxelWidth, 24);
    EXPECT_EQ(loaded.chunkVoxelHeight, 384);
    EXPECT_FLOAT_EQ(loaded.chunkWorldWidth, 16.0f);
    EXPECT_FLOAT_EQ(loaded.chunkWorldHeight, 256.0f);
}
