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
    Mountains = 5
};

enum class TerrainNoiseBasis : uint8_t
{
    OpenSimplex2 = 0,
    OpenSimplex2S = 1,
    Simplex = 2,
    Perlin = 3
};

struct TerrainNoiseSample
{
    float continentalness{};
    float erosion{};
    float peaksValleys{};
    float weirdness{};
};

struct WorldRegionSample
{
    float continentalness{};
};

struct TerrainColumnSample
{
    int surfaceHeight{};
    int stoneHeight{};
    BiomeType biome{BiomeType::None};
    BlockType topBlock{BlockType::STONE};
    BlockType fillerBlock{BlockType::STONE};
    bool isBeach{false};
    TerrainNoiseSample noise{};
};

struct TerrainNoiseLayerSettings
{
    TerrainNoiseBasis basis{TerrainNoiseBasis::OpenSimplex2};
    float frequency{0.0010f};
    int octaves{4};
    float lacunarity{2.0f};
    float gain{0.5f};
    float weightedStrength{0.0f};
    float remapFromMin{-1.0f};
    float remapFromMax{1.0f};
    float remapToMin{-1.0f};
    float remapToMax{1.0f};
    int terraceStepCount{1};
    float terraceSmoothness{0.0f};
    float strength{1.0f};
};

struct TerrainShapeSettings
{
    int seaLevel{static_cast<int>(SEA_LEVEL)};
    TerrainNoiseLayerSettings continental{
        .basis = TerrainNoiseBasis::OpenSimplex2S,
        .frequency = 0.00055f,
        .octaves = 5,
        .lacunarity = 2.0f,
        .gain = 0.5f,
        .weightedStrength = 0.0f,
        .terraceStepCount = 1,
        .terraceSmoothness = 0.0f,
        .strength = 1.0f
    };
    TerrainNoiseLayerSettings erosion{
        .basis = TerrainNoiseBasis::OpenSimplex2,
        .frequency = 0.00115f,
        .octaves = 4,
        .lacunarity = 2.0f,
        .gain = 0.5f,
        .weightedStrength = 0.0f,
        .terraceStepCount = 1,
        .terraceSmoothness = 0.0f,
        .strength = 1.0f
    };
    TerrainNoiseLayerSettings peaks{
        .basis = TerrainNoiseBasis::OpenSimplex2,
        .frequency = 0.00185f,
        .octaves = 4,
        .lacunarity = 2.0f,
        .gain = 0.5f,
        .weightedStrength = 0.0f,
        .terraceStepCount = 1,
        .terraceSmoothness = 0.0f,
        .strength = 1.0f
    };
    TerrainNoiseLayerSettings weirdness{
        .basis = TerrainNoiseBasis::OpenSimplex2S,
        .frequency = 0.00095f,
        .octaves = 3,
        .lacunarity = 2.0f,
        .gain = 0.5f,
        .weightedStrength = 0.0f,
        .terraceStepCount = 1,
        .terraceSmoothness = 0.0f,
        .strength = 1.0f
    };
};

struct TerrainDensitySettings
{
    TerrainNoiseBasis basis{TerrainNoiseBasis::OpenSimplex2S};
    float frequency{0.0090f};
    int octaves{4};
    float lacunarity{2.0f};
    float gain{0.5f};
    float weightedStrength{0.0f};
    float strength{1.0f};
    int maxBandHalfSpanBlocks{16};
};

struct TerrainGeneratorSettings
{
    uint32_t seed{1337};
    TerrainShapeSettings shape{};
    TerrainDensitySettings density{};
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
    CliffFace = 4,
    ScreeSlope = 5,
    UndersideRock = 6,
    InteriorStone = 7,
    SnowyTop = 8,
    SedimentLayer = 9
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
    FastNoise::SmartNode<> _weirdness;
    FastNoise::SmartNode<> _density;

    TerrainGeneratorSettings _settings{};

    mutable std::mutex _stateMutex;
    mutable std::unordered_map<ChunkCacheKey, WorldRegionScaffold2D, ChunkCacheKeyHash> _regionCache;
    mutable std::unordered_map<ChunkCacheKey, ChunkTerrainData, ChunkCacheKeyHash> _columnCache;
};
