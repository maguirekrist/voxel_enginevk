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

    [[nodiscard]] const ChunkTerrainData& GenerateChunkData(int chunkX, int chunkZ) const;
    [[nodiscard]] std::vector<float> GenerateHeightMap(int chunkX, int chunkZ) const;
    [[nodiscard]] TerrainColumnSample SampleColumn(int worldX, int worldZ) const;
    [[nodiscard]] float SampleHeight(int worldX, int worldZ) const;
    [[nodiscard]] float NormalizeHeight(std::vector<float>& map, int yScale, int xScale, int x, int y) const;

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

    FastNoise::SmartNode<> _erosion;
    FastNoise::SmartNode<> _peaks;
    FastNoise::SmartNode<> _continental;
    FastNoise::SmartNode<> _temperature;
    FastNoise::SmartNode<> _humidity;
    FastNoise::SmartNode<> _river;

    std::vector<SplinePoint> _erosionSplines;
    std::vector<SplinePoint> _peakSplines;
    std::vector<SplinePoint> _continentalSplines;

    uint32_t _seed{1337};
    std::vector<std::unique_ptr<IWorldGenLayer>> _layers;

    mutable std::mutex _cacheMutex;
    mutable std::unordered_map<ChunkCacheKey, ChunkTerrainData, ChunkCacheKeyHash> _chunkCache;
};
