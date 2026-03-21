#include "terrain_gen.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr float TerrainFrequency = 0.0010f;
    constexpr float ClimateFrequency = 0.00055f;
    constexpr float RiverFrequency = 0.0018f;
    constexpr float RiverThreshold = 0.07f;

    [[nodiscard]] int floor_to_int(const float value)
    {
        return static_cast<int>(std::floor(value));
    }

    [[nodiscard]] int wrap_to_chunk_axis(const int value, const int axisSize)
    {
        const int mod = value % axisSize;
        return mod < 0 ? mod + axisSize : mod;
    }

    [[nodiscard]] float clamp01(const float value)
    {
        return std::clamp(value, 0.0f, 1.0f);
    }

    [[nodiscard]] float inverse_lerp(const float minValue, const float maxValue, const float value)
    {
        if (std::abs(maxValue - minValue) <= std::numeric_limits<float>::epsilon())
        {
            return 0.0f;
        }

        return clamp01((value - minValue) / (maxValue - minValue));
    }

    class BaseTerrainLayer final : public IWorldGenLayer
    {
    public:
        BaseTerrainLayer(
            FastNoise::SmartNode<> erosion,
            FastNoise::SmartNode<> peaks,
            FastNoise::SmartNode<> continental,
            FastNoise::SmartNode<> temperature,
            FastNoise::SmartNode<> humidity,
            FastNoise::SmartNode<> river,
            const std::vector<SplinePoint>& erosionSplines,
            const std::vector<SplinePoint>& peakSplines,
            const std::vector<SplinePoint>& continentalSplines,
            const uint32_t seed) :
            _erosion(std::move(erosion)),
            _peaks(std::move(peaks)),
            _continental(std::move(continental)),
            _temperature(std::move(temperature)),
            _humidity(std::move(humidity)),
            _river(std::move(river)),
            _erosionSplines(erosionSplines),
            _peakSplines(peakSplines),
            _continentalSplines(continentalSplines),
            _seed(seed)
        {
        }

        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "BaseTerrain";
        }

        void apply(const WorldGenerationLayerContext& context, ChunkTerrainData& chunkData) const override
        {
            std::vector<float> erosionMap(CHUNK_SIZE * CHUNK_SIZE);
            std::vector<float> peaksMap(CHUNK_SIZE * CHUNK_SIZE);
            std::vector<float> continentalMap(CHUNK_SIZE * CHUNK_SIZE);
            std::vector<float> temperatureMap(CHUNK_SIZE * CHUNK_SIZE);
            std::vector<float> humidityMap(CHUNK_SIZE * CHUNK_SIZE);
            std::vector<float> riverMap(CHUNK_SIZE * CHUNK_SIZE);

            _erosion->GenUniformGrid2D(erosionMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, TerrainFrequency, _seed);
            _peaks->GenUniformGrid2D(peaksMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, TerrainFrequency, _seed);
            _continental->GenUniformGrid2D(continentalMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, TerrainFrequency, _seed);
            _temperature->GenUniformGrid2D(temperatureMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, ClimateFrequency, _seed + 101);
            _humidity->GenUniformGrid2D(humidityMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, ClimateFrequency, _seed + 202);
            _river->GenUniformGrid2D(riverMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, RiverFrequency, _seed + 303);

            for (int localZ = 0; localZ < static_cast<int>(CHUNK_SIZE); ++localZ)
            {
                for (int localX = 0; localX < static_cast<int>(CHUNK_SIZE); ++localX)
                {
                    const int index = localZ * static_cast<int>(CHUNK_SIZE) + localX;
                    TerrainColumnSample& column = chunkData.at(localX, localZ);
                    column.noise = TerrainNoiseSample{
                        .continentalness = continentalMap[index],
                        .erosion = erosionMap[index],
                        .peaksValleys = peaksMap[index],
                        .temperature = temperatureMap[index],
                        .humidity = humidityMap[index],
                        .river = riverMap[index]
                    };

                    const float continentalHeight = map_height(column.noise.continentalness, _continentalSplines);
                    const float peaksHeight = map_height(column.noise.peaksValleys, _peakSplines);
                    const float erosionSuppression = lerp(1.25f, 0.55f, inverse_lerp(-1.0f, 1.0f, column.noise.erosion));
                    const float mountainLift = peaksHeight * erosionSuppression;
                    const float height = std::clamp(continentalHeight + mountainLift, 1.0f, static_cast<float>(CHUNK_HEIGHT - 8));

                    const int surfaceHeight = static_cast<int>(std::round(height));
                    const int stoneDepth = std::max(3, static_cast<int>(std::round(map_height(column.noise.erosion, _erosionSplines) * 0.08f)));

                    column.surfaceHeight = surfaceHeight;
                    column.stoneHeight = std::max(0, surfaceHeight - stoneDepth);
                }
            }
        }

    private:
        [[nodiscard]] float map_height(const float noise, const std::vector<SplinePoint>& splinePoints) const
        {
            for (size_t i = 0; i + 1 < splinePoints.size(); ++i)
            {
                if (noise >= splinePoints[i].noiseValue && noise <= splinePoints[i + 1].noiseValue)
                {
                    const float t = (noise - splinePoints[i].noiseValue) /
                        (splinePoints[i + 1].noiseValue - splinePoints[i].noiseValue);
                    return lerp(splinePoints[i].heightValue, splinePoints[i + 1].heightValue, t);
                }
            }

            if (noise < splinePoints.front().noiseValue)
            {
                return splinePoints.front().heightValue;
            }

            if (noise > splinePoints.back().noiseValue)
            {
                return splinePoints.back().heightValue;
            }

            return 0.0f;
        }

        FastNoise::SmartNode<> _erosion;
        FastNoise::SmartNode<> _peaks;
        FastNoise::SmartNode<> _continental;
        FastNoise::SmartNode<> _temperature;
        FastNoise::SmartNode<> _humidity;
        FastNoise::SmartNode<> _river;
        std::vector<SplinePoint> _erosionSplines;
        std::vector<SplinePoint> _peakSplines;
        std::vector<SplinePoint> _continentalSplines;
        uint32_t _seed{};
    };

    class BiomeLayer final : public IWorldGenLayer
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "BiomeClassification";
        }

        void apply(const WorldGenerationLayerContext&, ChunkTerrainData& chunkData) const override
        {
            for (TerrainColumnSample& column : chunkData.columns)
            {
                const float temperature = inverse_lerp(-1.0f, 1.0f, column.noise.temperature);
                const float humidity = inverse_lerp(-1.0f, 1.0f, column.noise.humidity);
                const float riverProximity = 1.0f - clamp01(std::abs(column.noise.river) / RiverThreshold);

                column.hasRiver = riverProximity > 0.55f && column.surfaceHeight > static_cast<int>(SEA_LEVEL) - 4;
                column.isBeach = column.surfaceHeight >= static_cast<int>(SEA_LEVEL) - 2 &&
                    column.surfaceHeight <= static_cast<int>(SEA_LEVEL) + 3;

                if (column.surfaceHeight <= static_cast<int>(SEA_LEVEL) - 3 || column.noise.continentalness < -0.42f)
                {
                    column.biome = BiomeType::Ocean;
                }
                else if (column.hasRiver)
                {
                    column.biome = BiomeType::River;
                }
                else if (column.isBeach)
                {
                    column.biome = BiomeType::Shore;
                }
                else if (column.surfaceHeight >= static_cast<int>(SEA_LEVEL) + 45 || column.noise.peaksValleys > 0.38f)
                {
                    column.biome = BiomeType::Mountains;
                }
                else if (humidity > 0.55f && temperature > 0.35f)
                {
                    column.biome = BiomeType::Forest;
                }
                else
                {
                    column.biome = BiomeType::Plains;
                }
            }
        }
    };

    class SurfaceLayer final : public IWorldGenLayer
    {
    public:
        [[nodiscard]] std::string_view name() const noexcept override
        {
            return "SurfaceShaping";
        }

        void apply(const WorldGenerationLayerContext&, ChunkTerrainData& chunkData) const override
        {
            for (TerrainColumnSample& column : chunkData.columns)
            {
                if (column.biome == BiomeType::River)
                {
                    const float riverStrength = 1.0f - clamp01(std::abs(column.noise.river) / RiverThreshold);
                    const int riverTarget = static_cast<int>(SEA_LEVEL) - 2;
                    const int riverDepth = static_cast<int>(std::round(lerp(1.0f, 5.0f, riverStrength)));
                    column.surfaceHeight = std::max(3, riverTarget - riverDepth);
                    column.stoneHeight = std::max(0, column.surfaceHeight - 2);
                    column.topBlock = BlockType::SAND;
                    column.fillerBlock = BlockType::SAND;
                    continue;
                }

                if (column.biome == BiomeType::Ocean)
                {
                    column.surfaceHeight = std::min(column.surfaceHeight, static_cast<int>(SEA_LEVEL) - 4);
                    column.stoneHeight = std::max(0, column.surfaceHeight - 2);
                    column.topBlock = BlockType::SAND;
                    column.fillerBlock = BlockType::SAND;
                    continue;
                }

                if (column.biome == BiomeType::Shore)
                {
                    column.surfaceHeight = std::clamp(column.surfaceHeight, static_cast<int>(SEA_LEVEL) - 1, static_cast<int>(SEA_LEVEL) + 2);
                    column.stoneHeight = std::max(0, column.surfaceHeight - 3);
                    column.topBlock = BlockType::SAND;
                    column.fillerBlock = BlockType::SAND;
                    continue;
                }

                if (column.biome == BiomeType::Mountains)
                {
                    column.topBlock = column.surfaceHeight > static_cast<int>(SEA_LEVEL) + 70 ? BlockType::STONE : BlockType::GROUND;
                    column.fillerBlock = BlockType::STONE;
                    column.stoneHeight = std::max(0, column.surfaceHeight - 1);
                    continue;
                }

                column.topBlock = BlockType::GROUND;
                column.fillerBlock = BlockType::GROUND;
                column.stoneHeight = std::max(0, column.surfaceHeight - 4);
            }
        }
    };
}

const TerrainColumnSample& ChunkTerrainData::at(const int localX, const int localZ) const
{
    return columns[(localZ * static_cast<int>(CHUNK_SIZE)) + localX];
}

TerrainColumnSample& ChunkTerrainData::at(const int localX, const int localZ)
{
    return columns[(localZ * static_cast<int>(CHUNK_SIZE)) + localX];
}

size_t TerrainGenerator::ChunkCacheKeyHash::operator()(const ChunkCacheKey& key) const noexcept
{
    size_t seed = std::hash<int>()(key.x);
    seed ^= std::hash<int>()(key.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

TerrainGenerator::TerrainGenerator() :
    _erosion(FastNoise::NewFromEncodedNodeTree("FwAAAAAAexQuv83MTL5mZgbAIgApXJNBZmZmvxAAbxKDOg0ACAAAAArXI0AHAAB7FC4/AAAAgD8Aw/XwQQ==")),
    _peaks(FastNoise::NewFromEncodedNodeTree("EwCamRk/IgDXo3A/zcwMQBcAuB6FPpqZqcA9Cte+w/XoQCIAexS+QK5HwUAQAIXr0UAPAAkAAABcjyJACQAAXI/CPgB7FK4/AI/C9T0=")),
    _continental(FastNoise::NewFromEncodedNodeTree("FwAK16M8w/Wov7geBb8UrkdAIgCkcA1BCtejPA0ABAAAALgeZUAIAAAAAAA/AFyPwj8=")),
    _temperature(FastNoise::New<FastNoise::OpenSimplex2>()),
    _humidity(FastNoise::New<FastNoise::OpenSimplex2S>()),
    _river(FastNoise::New<FastNoise::Perlin>()),
    _erosionSplines({
        { -1.0f, 48.0f },
        { -0.1f, 64.0f },
        { 0.5f, 86.0f },
        { 1.0f, 110.0f }
    }),
    _peakSplines({
        { -1.0f, 0.0f },
        { -0.5f, 8.0f },
        { 0.0f, 24.0f },
        { 0.45f, 76.0f },
        { 1.0f, 132.0f }
    }),
    _continentalSplines({
        { -1.0f, 18.0f },
        { -0.45f, 34.0f },
        { -0.15f, 54.0f },
        { 0.10f, 72.0f },
        { 0.45f, 92.0f },
        { 1.0f, 116.0f }
    })
{
    _layers.push_back(std::make_unique<BaseTerrainLayer>(
        _erosion,
        _peaks,
        _continental,
        _temperature,
        _humidity,
        _river,
        _erosionSplines,
        _peakSplines,
        _continentalSplines,
        _seed));
    _layers.push_back(std::make_unique<BiomeLayer>());
    _layers.push_back(std::make_unique<SurfaceLayer>());
}

const ChunkTerrainData& TerrainGenerator::GenerateChunkData(const int chunkX, const int chunkZ) const
{
    const ChunkCacheKey key{chunkX, chunkZ};

    {
        std::scoped_lock lock(_cacheMutex);
        const auto it = _chunkCache.find(key);
        if (it != _chunkCache.end())
        {
            return it->second;
        }
    }

    ChunkTerrainData built = build_chunk_data(chunkX, chunkZ);

    std::scoped_lock lock(_cacheMutex);
    auto [it, inserted] = _chunkCache.emplace(key, std::move(built));
    return it->second;
}

std::vector<float> TerrainGenerator::GenerateHeightMap(const int chunkX, const int chunkZ) const
{
    const ChunkTerrainData& chunkData = GenerateChunkData(chunkX, chunkZ);
    std::vector<float> heightMap(CHUNK_SIZE * CHUNK_SIZE);

    for (int z = 0; z < static_cast<int>(CHUNK_SIZE); ++z)
    {
        for (int x = 0; x < static_cast<int>(CHUNK_SIZE); ++x)
        {
            heightMap[(z * static_cast<int>(CHUNK_SIZE)) + x] = static_cast<float>(chunkData.at(x, z).surfaceHeight);
        }
    }

    return heightMap;
}

TerrainColumnSample TerrainGenerator::SampleColumn(const int worldX, const int worldZ) const
{
    const int chunkOriginX = floor_to_int(static_cast<float>(worldX) / static_cast<float>(CHUNK_SIZE)) * static_cast<int>(CHUNK_SIZE);
    const int chunkOriginZ = floor_to_int(static_cast<float>(worldZ) / static_cast<float>(CHUNK_SIZE)) * static_cast<int>(CHUNK_SIZE);
    const ChunkTerrainData& chunkData = GenerateChunkData(chunkOriginX, chunkOriginZ);
    const int localX = wrap_to_chunk_axis(worldX, CHUNK_SIZE);
    const int localZ = wrap_to_chunk_axis(worldZ, CHUNK_SIZE);
    return chunkData.at(localX, localZ);
}

float TerrainGenerator::SampleHeight(const int worldX, const int worldZ) const
{
    return static_cast<float>(SampleColumn(worldX, worldZ).surfaceHeight);
}

float TerrainGenerator::NormalizeHeight(std::vector<float>& map, const int yScale, const int xScale, const int x, const int y) const
{
    const float height = map[(y * xScale) + x];
    const float normalized = (height + 1.0f) / 2.0f;
    return normalized * static_cast<float>(yScale);
}

ChunkTerrainData TerrainGenerator::build_chunk_data(const int chunkX, const int chunkZ) const
{
    ChunkTerrainData chunkData{
        .chunkOrigin = {chunkX, chunkZ},
        .columns = std::vector<TerrainColumnSample>(CHUNK_SIZE * CHUNK_SIZE)
    };

    const WorldGenerationLayerContext context{
        .chunkOrigin = {chunkX, chunkZ},
        .seed = _seed
    };

    for (const auto& layer : _layers)
    {
        layer->apply(context, chunkData);
    }

    return chunkData;
}

float TerrainGenerator::map_height(const float noise, const std::vector<SplinePoint>& splinePoints) const
{
    for (size_t i = 0; i + 1 < splinePoints.size(); ++i)
    {
        if (noise >= splinePoints[i].noiseValue && noise <= splinePoints[i + 1].noiseValue)
        {
            const float t = (noise - splinePoints[i].noiseValue) /
                (splinePoints[i + 1].noiseValue - splinePoints[i].noiseValue);
            return lerp(splinePoints[i].heightValue, splinePoints[i + 1].heightValue, t);
        }
    }

    if (noise < splinePoints.front().noiseValue)
    {
        return splinePoints.front().heightValue;
    }

    if (noise > splinePoints.back().noiseValue)
    {
        return splinePoints.back().heightValue;
    }

    return 0.0f;
}
