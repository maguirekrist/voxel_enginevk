#pragma once

#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <FastNoise/FastNoise.h>
#include <vk_types.h>

#include "constants.h"
#include "game/block.h"

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
    Ocean = 0,
    Shore = 1,
    Plains = 2,
    Forest = 3,
    River = 4,
    Mountains = 5
};

struct TerrainNoiseSample
{
    float continentalness{};
    float erosion{};
    float peaksValleys{};
    float detail{};
    float temperature{};
    float humidity{};
    float river{};
};

struct TerrainColumnSample
{
    int surfaceHeight{};
    int stoneHeight{};
    BiomeType biome{BiomeType::Plains};
    BlockType topBlock{BlockType::GROUND};
    BlockType fillerBlock{BlockType::GROUND};
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
    float climateFrequency{0.00055f};
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

struct TerrainBiomeSettings
{
    float oceanContinentalnessThreshold{-0.42f};
    float riverBlendThreshold{0.55f};
    int riverMinBankHeightOffset{-4};
    int beachMinHeightOffset{-2};
    int beachMaxHeightOffset{3};
    int mountainHeightOffset{45};
    float mountainPeaksThreshold{0.38f};
    float forestHumidityThreshold{0.55f};
    float forestTemperatureThreshold{0.35f};
    int mountainStoneHeightOffset{70};
};

struct TerrainSurfaceSettings
{
    int riverTargetHeightOffset{-2};
    int riverMinDepth{1};
    int riverMaxDepth{5};
    int oceanFloorHeightOffset{-4};
    int shoreMinHeightOffset{-1};
    int shoreMaxHeightOffset{2};
    int riverStoneDepth{2};
    int oceanStoneDepth{2};
    int shoreStoneDepth{3};
    int plainsStoneDepth{4};
    int mountainStoneDepth{1};
};

struct TerrainGeneratorSettings
{
    uint32_t seed{1337};
    TerrainShapeSettings shape{};
    TerrainBiomeSettings biome{};
    TerrainSurfaceSettings surface{};
    std::vector<SplinePoint> erosionSplines{};
    std::vector<SplinePoint> peakSplines{};
    std::vector<SplinePoint> continentalSplines{};
};

struct ChunkTerrainData
{
    glm::ivec2 chunkOrigin{};
    std::vector<TerrainColumnSample> columns{};

    [[nodiscard]] const TerrainColumnSample& at(int localX, int localZ) const;
    [[nodiscard]] TerrainColumnSample& at(int localX, int localZ);
};

struct WorldGenerationLayerContext
{
    glm::ivec2 chunkOrigin{};
    uint32_t seed{};
};

class IWorldGenLayer
{
public:
    virtual ~IWorldGenLayer() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual void apply(const WorldGenerationLayerContext& context, ChunkTerrainData& chunkData) const = 0;
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
    [[nodiscard]] std::vector<float> GenerateHeightMap(int chunkX, int chunkZ) const;
    [[nodiscard]] TerrainColumnSample SampleColumn(int worldX, int worldZ) const;
    [[nodiscard]] float SampleHeight(int worldX, int worldZ) const;
    [[nodiscard]] float NormalizeHeight(std::vector<float>& map, int yScale, int xScale, int x, int y) const;
    [[nodiscard]] TerrainGeneratorSettings settings() const;
    [[nodiscard]] static TerrainGeneratorSettings default_settings();
    void apply_settings(const TerrainGeneratorSettings& settings);

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

    [[nodiscard]] ChunkTerrainData build_chunk_data(int chunkX, int chunkZ) const;
    [[nodiscard]] float map_height(float noise, const std::vector<SplinePoint>& splinePoints) const;
    static void normalize_settings(TerrainGeneratorSettings& settings);
    void rebuild_layers();

    FastNoise::SmartNode<> _erosion;
    FastNoise::SmartNode<> _peaks;
    FastNoise::SmartNode<> _continental;
    FastNoise::SmartNode<> _detail;
    FastNoise::SmartNode<> _temperature;
    FastNoise::SmartNode<> _humidity;
    FastNoise::SmartNode<> _river;

    TerrainGeneratorSettings _settings{};
    std::vector<std::unique_ptr<IWorldGenLayer>> _layers;

    mutable std::mutex _stateMutex;
    mutable std::unordered_map<ChunkCacheKey, ChunkTerrainData, ChunkCacheKeyHash> _chunkCache;
};
