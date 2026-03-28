#include "terrain_gen.h"

#include <algorithm>
#include <ranges>

#include "game/chunk.h"
#include "generation/terrain_generation_helpers.h"

namespace
{
    constexpr std::array<SplinePoint, 4> DefaultErosionSplines{
        SplinePoint{ -1.0f, 48.0f },
        SplinePoint{ -0.1f, 64.0f },
        SplinePoint{ 0.5f, 86.0f },
        SplinePoint{ 1.0f, 110.0f }
    };

    constexpr std::array<SplinePoint, 5> DefaultPeakSplines{
        SplinePoint{ -1.0f, 0.0f },
        SplinePoint{ -0.5f, 8.0f },
        SplinePoint{ 0.0f, 24.0f },
        SplinePoint{ 0.45f, 76.0f },
        SplinePoint{ 1.0f, 132.0f }
    };

    constexpr std::array<SplinePoint, 6> DefaultContinentalSplines{
        SplinePoint{ -1.0f, 18.0f },
        SplinePoint{ -0.45f, 34.0f },
        SplinePoint{ -0.15f, 54.0f },
        SplinePoint{ 0.10f, 72.0f },
        SplinePoint{ 0.45f, 92.0f },
        SplinePoint{ 1.0f, 116.0f }
    };
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
    _detail(FastNoise::New<FastNoise::OpenSimplex2>()),
    _river(FastNoise::New<FastNoise::Perlin>()),
    _settings(default_settings())
{
    rebuild_noise();
}

WorldRegionScaffold2D TerrainGenerator::generate_region_scaffold(const int chunkX, const int chunkZ) const
{
    const ChunkCacheKey key{chunkX, chunkZ};
    {
        std::scoped_lock lock(_stateMutex);
        const auto it = _regionCache.find(key);
        if (it != _regionCache.end())
        {
            return it->second;
        }
    }

    WorldRegionScaffold2D scaffold{
        .chunkOrigin = {chunkX, chunkZ},
        .cells = std::vector<WorldRegionSample>(static_cast<size_t>(CHUNK_SIZE * CHUNK_SIZE))
    };
    terrain_generation::fill_region_scaffold(_continental, _river, _settings, scaffold.chunkOrigin, scaffold);

    std::scoped_lock lock(_stateMutex);
    auto [it, inserted] = _regionCache.emplace(key, scaffold);
    return it->second;
}

ChunkTerrainData TerrainGenerator::build_chunk_data(const int chunkX, const int chunkZ) const
{
    const WorldRegionScaffold2D regionScaffold = generate_region_scaffold(chunkX, chunkZ);

    ChunkTerrainData chunkData{
        .chunkOrigin = {chunkX, chunkZ},
        .columns = std::vector<TerrainColumnSample>(static_cast<size_t>(CHUNK_SIZE * CHUNK_SIZE))
    };

    terrain_generation::fill_column_scaffold(
        _erosion,
        _peaks,
        _detail,
        _settings,
        regionScaffold,
        chunkData.chunkOrigin,
        chunkData);

    return chunkData;
}

ChunkTerrainData TerrainGenerator::GenerateChunkData(const int chunkX, const int chunkZ) const
{
    const ChunkCacheKey key{chunkX, chunkZ};
    {
        std::scoped_lock lock(_stateMutex);
        const auto it = _columnCache.find(key);
        if (it != _columnCache.end())
        {
            return it->second;
        }
    }

    ChunkTerrainData built = build_chunk_data(chunkX, chunkZ);
    std::scoped_lock lock(_stateMutex);
    auto [it, inserted] = _columnCache.emplace(key, built);
    return it->second;
}

WorldGenerationChunkResult TerrainGenerator::GenerateChunkPipeline(const int chunkX, const int chunkZ) const
{
    WorldGenerationChunkResult result{};
    result.regionScaffold = generate_region_scaffold(chunkX, chunkZ);
    result.columnScaffold = GenerateChunkData(chunkX, chunkZ);
    result.featureInstances = TerrainFeatureInstanceSet{
        .chunkOrigin = {chunkX, chunkZ}
    };
    result.volumeBuffer = TerrainVolumeBuffer{
        .chunkOrigin = {chunkX, chunkZ},
        .cells = std::vector<TerrainVolumeCell>(static_cast<size_t>(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE))
    };
    terrain_generation::fill_heightfield_volume(result.columnScaffold, result.volumeBuffer.chunkOrigin, result.volumeBuffer);

    result.surfaceClassification = SurfaceClassificationBuffer{
        .chunkOrigin = {chunkX, chunkZ}
    };
    terrain_generation::clear_surface_classification(result.surfaceClassification);

    result.appearanceBuffer = AppearanceBuffer{
        .chunkOrigin = {chunkX, chunkZ}
    };
    terrain_generation::clear_appearance(result.appearanceBuffer);
    return result;
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
    const int chunkOriginX = terrain_generation::floor_to_int(static_cast<float>(worldX) / static_cast<float>(CHUNK_SIZE)) * static_cast<int>(CHUNK_SIZE);
    const int chunkOriginZ = terrain_generation::floor_to_int(static_cast<float>(worldZ) / static_cast<float>(CHUNK_SIZE)) * static_cast<int>(CHUNK_SIZE);
    const ChunkTerrainData chunkData = GenerateChunkData(chunkOriginX, chunkOriginZ);
    const int localX = terrain_generation::wrap_to_chunk_axis(worldX, CHUNK_SIZE);
    const int localZ = terrain_generation::wrap_to_chunk_axis(worldZ, CHUNK_SIZE);
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
    settings.erosionSplines.assign(DefaultErosionSplines.begin(), DefaultErosionSplines.end());
    settings.peakSplines.assign(DefaultPeakSplines.begin(), DefaultPeakSplines.end());
    settings.continentalSplines.assign(DefaultContinentalSplines.begin(), DefaultContinentalSplines.end());
    normalize_settings(settings);
    return settings;
}

int TerrainGenerator::sea_level() noexcept
{
    return instance().settings().shape.seaLevel;
}

void TerrainGenerator::apply_settings(const TerrainGeneratorSettings& settings)
{
    std::scoped_lock lock(_stateMutex);
    _settings = settings;
    normalize_settings(_settings);
    rebuild_noise();
    _regionCache.clear();
    _columnCache.clear();
}

void TerrainGenerator::RasterizeChunkTerrain(const WorldGenerationChunkResult& generation, ChunkData& chunkData) const
{
    PopulateBaseTerrainBlocks(generation.columnScaffold, chunkData);
}

void TerrainGenerator::PopulateBaseTerrainBlocks(const ChunkTerrainData& terrainData, ChunkData& chunkData) const
{
    const int seaLevel = _settings.shape.seaLevel;
    for (int x = 0; x < static_cast<int>(CHUNK_SIZE); ++x)
    {
        for (int z = 0; z < static_cast<int>(CHUNK_SIZE); ++z)
        {
            const TerrainColumnSample& column = terrainData.at(x, z);
            for (int y = 0; y < static_cast<int>(CHUNK_HEIGHT); ++y)
            {
                Block& block = chunkData.blocks[x][y][z];
                if (y > column.surfaceHeight)
                {
                    terrain_generation::set_air_or_water_block(seaLevel, y, block);
                    continue;
                }

                terrain_generation::set_solid_block(block, BlockType::STONE);
            }
        }
    }
}

void TerrainGenerator::ApplyVoxelTerrainFeatures(const ChunkTerrainData& terrainData, ChunkData& chunkData) const
{
    PopulateBaseTerrainBlocks(terrainData, chunkData);
}

void TerrainGenerator::RefreshSurfaceMaterials(const ChunkTerrainData&, ChunkData& chunkData) const
{
    for (int x = 0; x < static_cast<int>(CHUNK_SIZE); ++x)
    {
        for (int z = 0; z < static_cast<int>(CHUNK_SIZE); ++z)
        {
            for (int y = 0; y < static_cast<int>(CHUNK_HEIGHT); ++y)
            {
                Block& block = chunkData.blocks[x][y][z];
                if (block._solid)
                {
                    block._type = BlockType::STONE;
                }
            }
        }
    }
}

void TerrainGenerator::normalize_settings(TerrainGeneratorSettings& settings)
{
    auto normalize_spline = [](std::vector<SplinePoint>& spline, const auto& fallback)
    {
        if (spline.size() < 2)
        {
            spline.assign(fallback.begin(), fallback.end());
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

    settings.shape.continentalFrequency = std::max(settings.shape.continentalFrequency, 0.00001f);
    settings.shape.erosionFrequency = std::max(settings.shape.erosionFrequency, 0.00001f);
    settings.shape.peaksFrequency = std::max(settings.shape.peaksFrequency, 0.00001f);
    settings.shape.detailFrequency = std::max(settings.shape.detailFrequency, 0.00001f);
    settings.shape.seaLevel = std::clamp(settings.shape.seaLevel, 0, static_cast<int>(CHUNK_HEIGHT - 1));
    settings.shape.riverFrequency = std::max(settings.shape.riverFrequency, 0.00001f);
    settings.shape.riverThreshold = std::clamp(settings.shape.riverThreshold, 0.0001f, 1.0f);
    settings.shape.continentalStrength = std::clamp(settings.shape.continentalStrength, 0.0f, 3.0f);
    settings.shape.peaksStrength = std::clamp(settings.shape.peaksStrength, 0.0f, 3.0f);
    settings.shape.erosionStrength = std::clamp(settings.shape.erosionStrength, 0.0f, 2.0f);
    settings.shape.valleyStrength = std::clamp(settings.shape.valleyStrength, 0.0f, 96.0f);
    settings.shape.detailStrength = std::clamp(settings.shape.detailStrength, 0.0f, 32.0f);
    settings.shape.erosionSuppressionLow = std::clamp(settings.shape.erosionSuppressionLow, 0.0f, 4.0f);
    settings.shape.erosionSuppressionHigh = std::clamp(settings.shape.erosionSuppressionHigh, 0.0f, 4.0f);
    normalize_spline(settings.erosionSplines, DefaultErosionSplines);
    normalize_spline(settings.peakSplines, DefaultPeakSplines);
    normalize_spline(settings.continentalSplines, DefaultContinentalSplines);
}

void TerrainGenerator::rebuild_noise()
{
}
