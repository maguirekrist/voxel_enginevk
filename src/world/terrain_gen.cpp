#include "terrain_gen.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace
{
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
            const TerrainShapeSettings& shapeSettings,
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
            _shapeSettings(shapeSettings),
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

            _erosion->GenUniformGrid2D(erosionMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, _shapeSettings.terrainFrequency, _seed);
            _peaks->GenUniformGrid2D(peaksMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, _shapeSettings.terrainFrequency, _seed);
            _continental->GenUniformGrid2D(continentalMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, _shapeSettings.terrainFrequency, _seed);
            _temperature->GenUniformGrid2D(temperatureMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, _shapeSettings.climateFrequency, _seed + 101);
            _humidity->GenUniformGrid2D(humidityMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, _shapeSettings.climateFrequency, _seed + 202);
            _river->GenUniformGrid2D(riverMap.data(), context.chunkOrigin.x, context.chunkOrigin.y, CHUNK_SIZE, CHUNK_SIZE, _shapeSettings.riverFrequency, _seed + 303);

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
                    const float erosionSuppression = lerp(_shapeSettings.erosionSuppressionLow, _shapeSettings.erosionSuppressionHigh, inverse_lerp(-1.0f, 1.0f, column.noise.erosion));
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
        TerrainShapeSettings _shapeSettings{};
        std::vector<SplinePoint> _erosionSplines;
        std::vector<SplinePoint> _peakSplines;
        std::vector<SplinePoint> _continentalSplines;
        uint32_t _seed{};
    };

    class BiomeLayer final : public IWorldGenLayer
    {
    public:
        BiomeLayer(const TerrainBiomeSettings& settings, const TerrainShapeSettings& shapeSettings) :
            _settings(settings),
            _shapeSettings(shapeSettings)
        {
        }

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
                const float riverProximity = 1.0f - clamp01(std::abs(column.noise.river) / _shapeSettings.riverThreshold);

                column.hasRiver = riverProximity > _settings.riverBlendThreshold &&
                    column.surfaceHeight > static_cast<int>(SEA_LEVEL) + _settings.riverMinBankHeightOffset;
                column.isBeach = column.surfaceHeight >= static_cast<int>(SEA_LEVEL) + _settings.beachMinHeightOffset &&
                    column.surfaceHeight <= static_cast<int>(SEA_LEVEL) + _settings.beachMaxHeightOffset;

                if (column.surfaceHeight <= static_cast<int>(SEA_LEVEL) - 3 || column.noise.continentalness < _settings.oceanContinentalnessThreshold)
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
                else if (column.surfaceHeight >= static_cast<int>(SEA_LEVEL) + _settings.mountainHeightOffset ||
                    column.noise.peaksValleys > _settings.mountainPeaksThreshold)
                {
                    column.biome = BiomeType::Mountains;
                }
                else if (humidity > _settings.forestHumidityThreshold && temperature > _settings.forestTemperatureThreshold)
                {
                    column.biome = BiomeType::Forest;
                }
                else
                {
                    column.biome = BiomeType::Plains;
                }
            }
        }

    private:
        TerrainBiomeSettings _settings{};
        TerrainShapeSettings _shapeSettings{};
    };

    class SurfaceLayer final : public IWorldGenLayer
    {
    public:
        SurfaceLayer(const TerrainSurfaceSettings& surfaceSettings, const TerrainBiomeSettings& biomeSettings, const TerrainShapeSettings& shapeSettings) :
            _surfaceSettings(surfaceSettings),
            _biomeSettings(biomeSettings),
            _shapeSettings(shapeSettings)
        {
        }

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
                    const float riverStrength = 1.0f - clamp01(std::abs(column.noise.river) / _shapeSettings.riverThreshold);
                    const int riverTarget = static_cast<int>(SEA_LEVEL) + _surfaceSettings.riverTargetHeightOffset;
                    const int riverDepth = static_cast<int>(std::round(lerp(
                        static_cast<float>(_surfaceSettings.riverMinDepth),
                        static_cast<float>(_surfaceSettings.riverMaxDepth),
                        riverStrength)));
                    column.surfaceHeight = std::max(3, riverTarget - riverDepth);
                    column.stoneHeight = std::max(0, column.surfaceHeight - _surfaceSettings.riverStoneDepth);
                    column.topBlock = BlockType::SAND;
                    column.fillerBlock = BlockType::SAND;
                    continue;
                }

                if (column.biome == BiomeType::Ocean)
                {
                    column.surfaceHeight = std::min(column.surfaceHeight, static_cast<int>(SEA_LEVEL) + _surfaceSettings.oceanFloorHeightOffset);
                    column.stoneHeight = std::max(0, column.surfaceHeight - _surfaceSettings.oceanStoneDepth);
                    column.topBlock = BlockType::SAND;
                    column.fillerBlock = BlockType::SAND;
                    continue;
                }

                if (column.biome == BiomeType::Shore)
                {
                    column.surfaceHeight = std::clamp(
                        column.surfaceHeight,
                        static_cast<int>(SEA_LEVEL) + _surfaceSettings.shoreMinHeightOffset,
                        static_cast<int>(SEA_LEVEL) + _surfaceSettings.shoreMaxHeightOffset);
                    column.stoneHeight = std::max(0, column.surfaceHeight - _surfaceSettings.shoreStoneDepth);
                    column.topBlock = BlockType::SAND;
                    column.fillerBlock = BlockType::SAND;
                    continue;
                }

                if (column.biome == BiomeType::Mountains)
                {
                    column.topBlock = column.surfaceHeight > static_cast<int>(SEA_LEVEL) + _biomeSettings.mountainStoneHeightOffset ? BlockType::STONE : BlockType::GROUND;
                    column.fillerBlock = BlockType::STONE;
                    column.stoneHeight = std::max(0, column.surfaceHeight - _surfaceSettings.mountainStoneDepth);
                    continue;
                }

                column.topBlock = BlockType::GROUND;
                column.fillerBlock = BlockType::GROUND;
                column.stoneHeight = std::max(0, column.surfaceHeight - _surfaceSettings.plainsStoneDepth);
            }
        }

    private:
        TerrainSurfaceSettings _surfaceSettings{};
        TerrainBiomeSettings _biomeSettings{};
        TerrainShapeSettings _shapeSettings{};
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
    _settings(default_settings())
{
    rebuild_layers();
}

ChunkTerrainData TerrainGenerator::GenerateChunkData(const int chunkX, const int chunkZ) const
{
    const ChunkCacheKey key{chunkX, chunkZ};
    std::scoped_lock lock(_stateMutex);
    const auto it = _chunkCache.find(key);
    if (it != _chunkCache.end())
    {
        return it->second;
    }

    ChunkTerrainData built = build_chunk_data(chunkX, chunkZ);
    auto [insertedIt, inserted] = _chunkCache.emplace(key, built);
    return insertedIt->second;
}

std::vector<float> TerrainGenerator::GenerateHeightMap(const int chunkX, const int chunkZ) const
{
    const ChunkTerrainData chunkData = GenerateChunkData(chunkX, chunkZ);
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
    const ChunkTerrainData chunkData = GenerateChunkData(chunkOriginX, chunkOriginZ);
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

TerrainGeneratorSettings TerrainGenerator::settings() const
{
    std::scoped_lock lock(_stateMutex);
    return _settings;
}

TerrainGeneratorSettings TerrainGenerator::default_settings()
{
    TerrainGeneratorSettings settings{};
    settings.erosionSplines = {
        { -1.0f, 48.0f },
        { -0.1f, 64.0f },
        { 0.5f, 86.0f },
        { 1.0f, 110.0f }
    };
    settings.peakSplines = {
        { -1.0f, 0.0f },
        { -0.5f, 8.0f },
        { 0.0f, 24.0f },
        { 0.45f, 76.0f },
        { 1.0f, 132.0f }
    };
    settings.continentalSplines = {
        { -1.0f, 18.0f },
        { -0.45f, 34.0f },
        { -0.15f, 54.0f },
        { 0.10f, 72.0f },
        { 0.45f, 92.0f },
        { 1.0f, 116.0f }
    };
    normalize_settings(settings);
    return settings;
}

void TerrainGenerator::apply_settings(const TerrainGeneratorSettings& settings)
{
    std::scoped_lock lock(_stateMutex);
    _settings = settings;
    normalize_settings(_settings);
    rebuild_layers();
    _chunkCache.clear();
}

ChunkTerrainData TerrainGenerator::build_chunk_data(const int chunkX, const int chunkZ) const
{
    ChunkTerrainData chunkData{
        .chunkOrigin = {chunkX, chunkZ},
        .columns = std::vector<TerrainColumnSample>(CHUNK_SIZE * CHUNK_SIZE)
    };

    const WorldGenerationLayerContext context{
        .chunkOrigin = {chunkX, chunkZ},
        .seed = _settings.seed
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

void TerrainGenerator::normalize_settings(TerrainGeneratorSettings& settings)
{
    auto normalize_spline = [](std::vector<SplinePoint>& spline, const std::vector<SplinePoint>& fallback)
    {
        if (spline.size() < 2)
        {
            spline = fallback;
        }

        std::ranges::sort(spline, [](const SplinePoint& lhs, const SplinePoint& rhs)
        {
            return lhs.noiseValue < rhs.noiseValue;
        });

        for (SplinePoint& point : spline)
        {
            point.noiseValue = std::clamp(point.noiseValue, -1.0f, 1.0f);
            point.heightValue = std::clamp(point.heightValue, 0.0f, static_cast<float>(CHUNK_HEIGHT - 1));
        }
    };

    settings.shape.terrainFrequency = std::max(settings.shape.terrainFrequency, 0.00001f);
    settings.shape.climateFrequency = std::max(settings.shape.climateFrequency, 0.00001f);
    settings.shape.riverFrequency = std::max(settings.shape.riverFrequency, 0.00001f);
    settings.shape.riverThreshold = std::clamp(settings.shape.riverThreshold, 0.0001f, 1.0f);
    settings.shape.erosionSuppressionLow = std::clamp(settings.shape.erosionSuppressionLow, 0.0f, 4.0f);
    settings.shape.erosionSuppressionHigh = std::clamp(settings.shape.erosionSuppressionHigh, 0.0f, 4.0f);
    settings.biome.oceanContinentalnessThreshold = std::clamp(settings.biome.oceanContinentalnessThreshold, -1.0f, 1.0f);
    settings.biome.riverBlendThreshold = std::clamp(settings.biome.riverBlendThreshold, 0.0001f, 1.0f);
    settings.biome.beachMinHeightOffset = std::min(settings.biome.beachMinHeightOffset, settings.biome.beachMaxHeightOffset);
    settings.surface.shoreMinHeightOffset = std::min(settings.surface.shoreMinHeightOffset, settings.surface.shoreMaxHeightOffset);
    settings.surface.riverMinDepth = std::max(settings.surface.riverMinDepth, 1);
    settings.surface.riverMaxDepth = std::max(settings.surface.riverMaxDepth, settings.surface.riverMinDepth);
    settings.surface.riverStoneDepth = std::max(settings.surface.riverStoneDepth, 1);
    settings.surface.oceanStoneDepth = std::max(settings.surface.oceanStoneDepth, 1);
    settings.surface.shoreStoneDepth = std::max(settings.surface.shoreStoneDepth, 1);
    settings.surface.plainsStoneDepth = std::max(settings.surface.plainsStoneDepth, 1);
    settings.surface.mountainStoneDepth = std::max(settings.surface.mountainStoneDepth, 1);

    const std::vector<SplinePoint> defaultErosionSplines{
        { -1.0f, 48.0f },
        { -0.1f, 64.0f },
        { 0.5f, 86.0f },
        { 1.0f, 110.0f }
    };
    const std::vector<SplinePoint> defaultPeakSplines{
        { -1.0f, 0.0f },
        { -0.5f, 8.0f },
        { 0.0f, 24.0f },
        { 0.45f, 76.0f },
        { 1.0f, 132.0f }
    };
    const std::vector<SplinePoint> defaultContinentalSplines{
        { -1.0f, 18.0f },
        { -0.45f, 34.0f },
        { -0.15f, 54.0f },
        { 0.10f, 72.0f },
        { 0.45f, 92.0f },
        { 1.0f, 116.0f }
    };
    normalize_spline(settings.erosionSplines, defaultErosionSplines);
    normalize_spline(settings.peakSplines, defaultPeakSplines);
    normalize_spline(settings.continentalSplines, defaultContinentalSplines);
}

void TerrainGenerator::rebuild_layers()
{
    _layers.clear();
    _layers.push_back(std::make_unique<BaseTerrainLayer>(
        _erosion,
        _peaks,
        _continental,
        _temperature,
        _humidity,
        _river,
        _settings.shape,
        _settings.erosionSplines,
        _settings.peakSplines,
        _settings.continentalSplines,
        _settings.seed));
    _layers.push_back(std::make_unique<BiomeLayer>(_settings.biome, _settings.shape));
    _layers.push_back(std::make_unique<SurfaceLayer>(_settings.surface, _settings.biome, _settings.shape));
}
