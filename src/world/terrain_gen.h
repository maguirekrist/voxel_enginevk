#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <FastNoise/FastNoise.h>
#include <vk_types.h>

#include "constants.h"
#include "game/block.h"

struct ChunkData;

struct SplinePoint
{
    float noiseValue{};
    float heightValue{};
};

constexpr float lerp(const float startValue, const float endValue, const float t)
{
    return (1.0f - t) * startValue + t * endValue;
}

enum class BiomeType : uint8_t
{
    None = 0,
    Ocean = 1,
    Shore = 2,
    Plains = 3,
    Forest = 4,
    River = 5,
    Mountains = 6
};

struct TerrainNoiseSample
{
    float continentalness{};
    float erosion{};
    float peaksValleys{};
    float detail{};
    float river{};
};

struct WorldRegionSample
{
    float continentalness{};
    float riverPotential{};
};

struct TerrainColumnSample
{
    int surfaceHeight{};
    int baseSurfaceHeight{};
    int stoneHeight{};
    BiomeType biome{BiomeType::None};
    BlockType topBlock{BlockType::STONE};
    BlockType fillerBlock{BlockType::STONE};
    bool hasRiver{false};
    bool isBeach{false};
    TerrainNoiseSample noise{};
};

struct TerrainShapeSettings
{
    float continentalFrequency{0.00055f};
    float erosionFrequency{0.00115f};
    float peaksFrequency{0.00185f};
    float detailFrequency{0.0065f};
    int seaLevel{static_cast<int>(SEA_LEVEL)};
    bool riversEnabled{true};
    float riverFrequency{0.0018f};
    float riverThreshold{0.07f};
    float continentalStrength{1.0f};
    float peaksStrength{0.9f};
    float erosionStrength{1.0f};
    float valleyStrength{28.0f};
    float detailStrength{5.0f};
    float erosionSuppressionLow{1.25f};
    float erosionSuppressionHigh{0.55f};
};

struct TerrainGeneratorSettings
{
    uint32_t seed{1337};
    TerrainShapeSettings shape{};
    std::vector<SplinePoint> erosionSplines{};
    std::vector<SplinePoint> peakSplines{};
    std::vector<SplinePoint> continentalSplines{};
};

struct WorldRegionScaffold2D
{
    glm::ivec2 chunkOrigin{};
    std::vector<WorldRegionSample> cells{};

    [[nodiscard]] const WorldRegionSample& at(int localX, int localZ) const;
    [[nodiscard]] WorldRegionSample& at(int localX, int localZ);
};

struct TerrainColumnScaffold2D
{
    glm::ivec2 chunkOrigin{};
    std::vector<TerrainColumnSample> columns{};

    [[nodiscard]] const TerrainColumnSample& at(int localX, int localZ) const;
    [[nodiscard]] TerrainColumnSample& at(int localX, int localZ);
};

using ChunkTerrainData = TerrainColumnScaffold2D;

enum class TerrainFeatureType : uint8_t
{
    MountainMass = 1,
    ShelfOverhang = 2
};

struct TerrainFeatureInstance
{
    uint32_t id{};
    TerrainFeatureType type{TerrainFeatureType::MountainMass};
    uint64_t seed{};
    glm::ivec3 minBounds{};
    glm::ivec3 maxBounds{};
    glm::vec3 center{0.0f};
    glm::vec3 radii{0.0f};
    glm::vec2 directionXZ{0.0f};
    float thickness{};
    float noiseFrequency{};
    float noiseStrength{};
    BiomeType biome{BiomeType::None};
    float styleWeight{};
};

struct TerrainFeatureInstanceSet
{
    glm::ivec2 chunkOrigin{};
    std::vector<TerrainFeatureInstance> features{};

    [[nodiscard]] const TerrainFeatureInstance* find_by_id(uint32_t id) const;
};

enum class MaterialClass : uint8_t
{
    Air = 0,
    Ground = 1,
    Stone = 2,
    Sand = 3,
    Snow = 4,
    Wood = 5,
    Leaves = 6,
    Water = 7,
    Cloud = 8
};

struct TerrainVolumeCell
{
    float density{-1.0f};
    MaterialClass material{MaterialClass::Air};
    uint32_t featureId{};
    float surfaceAffinity{};
};

struct TerrainVolumeBuffer
{
    glm::ivec2 chunkOrigin{};
    std::vector<TerrainVolumeCell> cells{};

    [[nodiscard]] const TerrainVolumeCell& at(int localX, int y, int localZ) const;
    [[nodiscard]] TerrainVolumeCell& at(int localX, int y, int localZ);
};

enum class SurfaceClass : uint8_t
{
    None = 0,
    GrassTop = 1,
    ForestFloor = 2,
    BeachTop = 3,
    WetRiverbank = 4,
    CliffFace = 5,
    ScreeSlope = 6,
    UndersideRock = 7,
    InteriorStone = 8,
    SnowyTop = 9,
    SedimentLayer = 10
};

struct SurfaceClassificationBuffer
{
    glm::ivec2 chunkOrigin{};
    std::vector<std::array<SurfaceClass, 6>> faces{};

    [[nodiscard]] const std::array<SurfaceClass, 6>& at(int localX, int y, int localZ) const;
    [[nodiscard]] std::array<SurfaceClass, 6>& at(int localX, int y, int localZ);
};

[[nodiscard]] constexpr uint32_t pack_appearance_color(const glm::u8vec3 color) noexcept
{
    return static_cast<uint32_t>(color.r) |
        (static_cast<uint32_t>(color.g) << 8) |
        (static_cast<uint32_t>(color.b) << 16) |
        (0xFFu << 24);
}

[[nodiscard]] uint32_t pack_appearance_color(const glm::vec3& color) noexcept;
[[nodiscard]] glm::vec3 unpack_appearance_color(uint32_t packedColor) noexcept;

struct TerrainAppearanceVoxel
{
    uint32_t color{};
};

struct AppearanceBuffer
{
    glm::ivec2 chunkOrigin{};
    std::vector<TerrainAppearanceVoxel> voxels{};

    [[nodiscard]] const TerrainAppearanceVoxel& at(int localX, int y, int localZ) const;
    [[nodiscard]] TerrainAppearanceVoxel& at(int localX, int y, int localZ);
    [[nodiscard]] uint32_t packed_color(int localX, int y, int localZ) const;
};

struct WorldGenerationChunkResult
{
    WorldRegionScaffold2D regionScaffold{};
    TerrainColumnScaffold2D columnScaffold{};
    TerrainFeatureInstanceSet featureInstances{};
    TerrainVolumeBuffer volumeBuffer{};
    SurfaceClassificationBuffer surfaceClassification{};
    AppearanceBuffer appearanceBuffer{};
};

class TerrainGenerator
{
public:
    static TerrainGenerator& instance()
    {
        static TerrainGenerator instance;
        return instance;
    }

    [[nodiscard]] ChunkTerrainData GenerateChunkData(int chunkX, int chunkZ) const;
    [[nodiscard]] WorldGenerationChunkResult GenerateChunkPipeline(int chunkX, int chunkZ) const;
    [[nodiscard]] std::vector<float> GenerateHeightMap(int chunkX, int chunkZ) const;
    [[nodiscard]] TerrainColumnSample SampleColumn(int worldX, int worldZ) const;
    [[nodiscard]] float SampleHeight(int worldX, int worldZ) const;
    [[nodiscard]] float NormalizeHeight(std::vector<float>& map, int yScale, int xScale, int x, int y) const;
    [[nodiscard]] TerrainGeneratorSettings settings() const;
    [[nodiscard]] static TerrainGeneratorSettings default_settings();
    [[nodiscard]] static int sea_level() noexcept;
    void apply_settings(const TerrainGeneratorSettings& settings);
    void RasterizeChunkTerrain(const WorldGenerationChunkResult& generation, ChunkData& chunkData) const;
    void PopulateBaseTerrainBlocks(const ChunkTerrainData& terrainData, ChunkData& chunkData) const;
    void ApplyVoxelTerrainFeatures(const ChunkTerrainData& terrainData, ChunkData& chunkData) const;
    void RefreshSurfaceMaterials(const ChunkTerrainData& terrainData, ChunkData& chunkData) const;

private:
    struct ChunkCacheKey
    {
        int x{};
        int z{};

        [[nodiscard]] bool operator==(const ChunkCacheKey& other) const noexcept = default;
    };

    struct ChunkCacheKeyHash
    {
        [[nodiscard]] size_t operator()(const ChunkCacheKey& key) const noexcept;
    };

    TerrainGenerator();
    ~TerrainGenerator() = default;

    [[nodiscard]] WorldRegionScaffold2D generate_region_scaffold(int chunkX, int chunkZ) const;
    [[nodiscard]] ChunkTerrainData build_chunk_data(int chunkX, int chunkZ) const;
    static void normalize_settings(TerrainGeneratorSettings& settings);
    void rebuild_noise();

    FastNoise::SmartNode<> _erosion;
    FastNoise::SmartNode<> _peaks;
    FastNoise::SmartNode<> _continental;
    FastNoise::SmartNode<> _detail;
    FastNoise::SmartNode<> _river;

    TerrainGeneratorSettings _settings{};

    mutable std::mutex _stateMutex;
    mutable std::unordered_map<ChunkCacheKey, WorldRegionScaffold2D, ChunkCacheKeyHash> _regionCache;
    mutable std::unordered_map<ChunkCacheKey, ChunkTerrainData, ChunkCacheKeyHash> _columnCache;
};
