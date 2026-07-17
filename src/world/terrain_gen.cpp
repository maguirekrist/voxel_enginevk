#include "terrain_gen.h"

#include <algorithm>
#include <cmath>
#include <ranges>

#include <tracy/Tracy.hpp>

#include "game/chunk.h"
#include "generation/terrain_generation_helpers.h"

namespace
{
    constexpr float MinShapeFrequency = 0.00005f;
    constexpr float MaxShapeFrequency = 0.0050f;
    constexpr float MinShapeStrength = 0.0f;
    constexpr float MaxShapeStrength = 1.0f;
    constexpr int MinNoiseOctaves = 1;
    constexpr int MaxNoiseOctaves = 16;
    constexpr int MinTerraceStepCount = 1;
    constexpr int MaxTerraceStepCount = 32;
    constexpr float MinNoiseLacunarity = 1.0f;
    constexpr float MaxNoiseLacunarity = 4.0f;
    constexpr float MinNoiseGain = 0.0f;
    constexpr float MaxNoiseGain = 1.0f;
    constexpr float MinNoiseWeightedStrength = 0.0f;
    constexpr float MaxNoiseWeightedStrength = 1.0f;
    constexpr float MinNoiseRemapValue = -1.0f;
    constexpr float MaxNoiseRemapValue = 1.0f;
    constexpr float MinTerraceSmoothness = 0.0f;
    constexpr float MaxTerraceSmoothness = 1.0f;
    constexpr int MinDensityBandHalfSpanBlocks = 0;
    constexpr float MinDensitySampleCellBlocks = 1.0f;
    constexpr float MaxDensitySampleCellBlocks = 64.0f;

    [[nodiscard]] FastNoise::SmartNode<> build_noise_source(const TerrainNoiseBasis basis)
    {
        switch (basis)
        {
        case TerrainNoiseBasis::OpenSimplex2S:
            return FastNoise::New<FastNoise::OpenSimplex2S>();
        case TerrainNoiseBasis::Simplex:
            return FastNoise::New<FastNoise::Simplex>();
        case TerrainNoiseBasis::Perlin:
            return FastNoise::New<FastNoise::Perlin>();
        case TerrainNoiseBasis::OpenSimplex2:
        default:
            return FastNoise::New<FastNoise::OpenSimplex2>();
        }
    }

    [[nodiscard]] FastNoise::SmartNode<> build_noise_layer(const TerrainNoiseLayerSettings& settings, const bool allowTerrace = true)
    {
        FastNoise::SmartNode<> node = build_noise_source(settings.basis);
        if (settings.octaves <= 1)
        {
            if (!allowTerrace || settings.terraceStepCount <= 1)
            {
                return node;
            }
        }
        else
        {
            auto fractal = FastNoise::New<FastNoise::FractalFBm>();
            fractal->SetSource(node);
            fractal->SetOctaveCount(settings.octaves);
            fractal->SetLacunarity(settings.lacunarity);
            fractal->SetGain(settings.gain);
            fractal->SetWeightedStrength(settings.weightedStrength);
            node = fractal;
        }

        if (allowTerrace && settings.terraceStepCount > 1)
        {
            auto terrace = FastNoise::New<FastNoise::Terrace>();
            terrace->SetSource(node);
            terrace->SetMultiplier(static_cast<float>(settings.terraceStepCount));
            terrace->SetSmoothness(settings.terraceSmoothness);
            node = terrace;
        }

        if (settings.remapFromMin != settings.remapToMin ||
            settings.remapFromMax != settings.remapToMax)
        {
            auto remap = FastNoise::New<FastNoise::Remap>();
            remap->SetSource(node);
            remap->SetRemap(
                settings.remapFromMin,
                settings.remapFromMax,
                settings.remapToMin,
                settings.remapToMax);
            node = remap;
        }

        return node;
    }

    void normalize_noise_layer(TerrainNoiseLayerSettings& settings)
    {
        settings.frequency = std::clamp(settings.frequency, MinShapeFrequency, MaxShapeFrequency);
        settings.octaves = std::clamp(settings.octaves, MinNoiseOctaves, MaxNoiseOctaves);
        settings.lacunarity = std::clamp(settings.lacunarity, MinNoiseLacunarity, MaxNoiseLacunarity);
        settings.gain = std::clamp(settings.gain, MinNoiseGain, MaxNoiseGain);
        settings.weightedStrength = std::clamp(settings.weightedStrength, MinNoiseWeightedStrength, MaxNoiseWeightedStrength);
        settings.remapFromMin = std::clamp(settings.remapFromMin, MinNoiseRemapValue, MaxNoiseRemapValue);
        settings.remapFromMax = std::clamp(settings.remapFromMax, MinNoiseRemapValue, MaxNoiseRemapValue);
        settings.remapToMin = std::clamp(settings.remapToMin, MinNoiseRemapValue, MaxNoiseRemapValue);
        settings.remapToMax = std::clamp(settings.remapToMax, MinNoiseRemapValue, MaxNoiseRemapValue);
        if (settings.remapFromMax <= settings.remapFromMin)
        {
            settings.remapFromMax = std::min(MaxNoiseRemapValue, settings.remapFromMin + 0.01f);
        }
        if (settings.remapToMax <= settings.remapToMin)
        {
            settings.remapToMax = std::min(MaxNoiseRemapValue, settings.remapToMin + 0.01f);
        }
        settings.terraceStepCount = std::clamp(settings.terraceStepCount, MinTerraceStepCount, MaxTerraceStepCount);
        settings.terraceSmoothness = std::clamp(settings.terraceSmoothness, MinTerraceSmoothness, MaxTerraceSmoothness);
        settings.strength = std::clamp(settings.strength, MinShapeStrength, MaxShapeStrength);
    }

    void normalize_density_settings(TerrainDensitySettings& settings, const int maxBandHalfSpanBlocks)
    {
        settings.frequency = std::clamp(settings.frequency, MinShapeFrequency, MaxShapeFrequency);
        settings.octaves = std::clamp(settings.octaves, MinNoiseOctaves, MaxNoiseOctaves);
        settings.lacunarity = std::clamp(settings.lacunarity, MinNoiseLacunarity, MaxNoiseLacunarity);
        settings.gain = std::clamp(settings.gain, MinNoiseGain, MaxNoiseGain);
        settings.weightedStrength = std::clamp(settings.weightedStrength, MinNoiseWeightedStrength, MaxNoiseWeightedStrength);
        settings.strength = std::clamp(settings.strength, MinShapeStrength, MaxShapeStrength);
        settings.maxBandHalfSpanBlocks = std::clamp(
            settings.maxBandHalfSpanBlocks,
            MinDensityBandHalfSpanBlocks,
            std::max(MinDensityBandHalfSpanBlocks, maxBandHalfSpanBlocks));
        settings.sampleCellSizeBlocks = std::clamp(
            settings.sampleCellSizeBlocks,
            MinDensitySampleCellBlocks,
            MaxDensitySampleCellBlocks);
    }

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

    [[nodiscard]] int world_units_to_voxels(const float value, const float blockWorldSize) noexcept
    {
        return std::max(0, static_cast<int>(std::lround(value / blockWorldSize)));
    }

    [[nodiscard]] float max_world_height(const int chunkVoxelHeight, const float blockWorldSize) noexcept
    {
        return static_cast<float>(std::max(0, chunkVoxelHeight - 1)) * blockWorldSize;
    }
}

size_t TerrainGenerator::ChunkCacheKeyHash::operator()(const ChunkCacheKey& key) const noexcept
{
    size_t seed = std::hash<int>()(key.x);
    seed ^= std::hash<int>()(key.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

TerrainGenerator::TerrainGenerator() :
    _erosion(FastNoise::New<FastNoise::OpenSimplex2>()),
    _peaks(FastNoise::New<FastNoise::OpenSimplex2>()),
    _continental(FastNoise::New<FastNoise::OpenSimplex2S>()),
    _weirdness(FastNoise::New<FastNoise::OpenSimplex2S>()),
    _density(FastNoise::New<FastNoise::OpenSimplex2S>()),
    _settings(default_settings())
{
    rebuild_noise();
}

WorldRegionScaffold2D TerrainGenerator::build_region_scaffold(const int chunkX, const int chunkZ) const
{
    ZoneScopedN("TerrainGenerator::BuildRegionScaffold");
    WorldRegionScaffold2D scaffold{
        .chunkOrigin = {chunkX, chunkZ},
        .chunkVoxelWidth = _chunkVoxelWidth,
        .chunkVoxelHeight = _chunkVoxelHeight,
        .cells = std::vector<WorldRegionSample>(static_cast<size_t>(_chunkVoxelWidth * _chunkVoxelWidth))
    };
    terrain_generation::fill_region_scaffold(_continental, _settings, scaffold.chunkOrigin, _blockWorldSize, scaffold);

    return scaffold;
}

const WorldRegionScaffold2D& TerrainGenerator::region_scaffold_for(const int chunkX, const int chunkZ) const
{
    ZoneScopedN("TerrainGenerator::RegionScaffoldCache");
    const ChunkCacheKey key{chunkX, chunkZ};
    {
        std::scoped_lock lock(_stateMutex);
        const auto it = _regionCache.find(key);
        if (it != _regionCache.end())
        {
            ZoneText("hit", 3);
            return it->second;
        }
    }

    ZoneText("miss", 4);
    WorldRegionScaffold2D scaffold = build_region_scaffold(chunkX, chunkZ);
    std::scoped_lock lock(_stateMutex);
    auto [it, inserted] = _regionCache.emplace(key, std::move(scaffold));
    return it->second;
}

ChunkTerrainData TerrainGenerator::build_chunk_data(const int chunkX, const int chunkZ) const
{
    ZoneScopedN("TerrainGenerator::BuildChunkData");
    const WorldRegionScaffold2D& regionScaffold = region_scaffold_for(chunkX, chunkZ);

    ChunkTerrainData chunkData{
        .chunkOrigin = {chunkX, chunkZ},
        .chunkVoxelWidth = _chunkVoxelWidth,
        .chunkVoxelHeight = _chunkVoxelHeight,
        .columns = std::vector<TerrainColumnSample>(static_cast<size_t>(_chunkVoxelWidth * _chunkVoxelWidth))
    };

    terrain_generation::fill_column_scaffold(
        _erosion,
        _peaks,
        _weirdness,
        _settings,
        regionScaffold,
        chunkData.chunkOrigin,
        _blockWorldSize,
        chunkData);

    return chunkData;
}

const ChunkTerrainData& TerrainGenerator::chunk_data_for(const int chunkX, const int chunkZ) const
{
    ZoneScopedN("TerrainGenerator::ChunkDataCache");
    const ChunkCacheKey key{chunkX, chunkZ};
    {
        std::scoped_lock lock(_stateMutex);
        const auto it = _columnCache.find(key);
        if (it != _columnCache.end())
        {
            ZoneText("hit", 3);
            return it->second;
        }
    }

    ZoneText("miss", 4);
    ChunkTerrainData built = build_chunk_data(chunkX, chunkZ);
    std::scoped_lock lock(_stateMutex);
    auto [it, inserted] = _columnCache.emplace(key, std::move(built));
    return it->second;
}

ChunkTerrainData TerrainGenerator::GenerateChunkData(const int chunkX, const int chunkZ) const
{
    return chunk_data_for(chunkX, chunkZ);
}

WorldGenerationChunkResult TerrainGenerator::GenerateChunkPipeline(const int chunkX, const int chunkZ) const
{
    ZoneScopedN("TerrainGenerator::GenerateChunkPipeline");
    WorldGenerationChunkResult result{};
    {
        ZoneScopedN("TerrainGenerator::CopyRegionScaffold");
        result.regionScaffold = region_scaffold_for(chunkX, chunkZ);
    }
    {
        ZoneScopedN("TerrainGenerator::CopyColumnScaffold");
        result.columnScaffold = chunk_data_for(chunkX, chunkZ);
    }
    result.featureInstances = TerrainFeatureInstanceSet{
        .chunkOrigin = {chunkX, chunkZ}
    };
    {
        ZoneScopedN("TerrainGenerator::BuildDensityVolume");
        result.volumeBuffer = TerrainVolumeBuffer{
            .chunkOrigin = {chunkX, chunkZ},
            .chunkVoxelWidth = _chunkVoxelWidth,
            .chunkVoxelHeight = _chunkVoxelHeight,
            .cells = std::vector<TerrainVolumeCell>(static_cast<size_t>(_chunkVoxelWidth * _chunkVoxelHeight * _chunkVoxelWidth))
        };
        terrain_generation::fill_density_volume(
            result.columnScaffold,
            _density,
            _settings,
            result.volumeBuffer.chunkOrigin,
            _blockWorldSize,
            result.volumeBuffer);
    }

    result.surfaceClassification = SurfaceClassificationBuffer{
        .chunkOrigin = {chunkX, chunkZ},
        .chunkVoxelWidth = _chunkVoxelWidth,
        .chunkVoxelHeight = _chunkVoxelHeight
    };

    result.appearanceBuffer = AppearanceBuffer{
        .chunkOrigin = {chunkX, chunkZ},
        .chunkVoxelWidth = _chunkVoxelWidth,
        .chunkVoxelHeight = _chunkVoxelHeight
    };
    return result;
}

std::vector<float> TerrainGenerator::GenerateHeightMap(const int chunkX, const int chunkZ) const
{
    const ChunkTerrainData& chunkData = chunk_data_for(chunkX, chunkZ);
    std::vector<float> heightMap(static_cast<size_t>(_chunkVoxelWidth * _chunkVoxelWidth));

    for (int z = 0; z < _chunkVoxelWidth; ++z)
    {
        for (int x = 0; x < _chunkVoxelWidth; ++x)
        {
            heightMap[(z * _chunkVoxelWidth) + x] = static_cast<float>(chunkData.at(x, z).surfaceHeight);
        }
    }

    return heightMap;
}

TerrainColumnSample TerrainGenerator::SampleColumn(const int worldX, const int worldZ) const
{
    const int chunkOriginX = terrain_generation::floor_to_int(static_cast<float>(worldX) / static_cast<float>(_chunkVoxelWidth)) * _chunkVoxelWidth;
    const int chunkOriginZ = terrain_generation::floor_to_int(static_cast<float>(worldZ) / static_cast<float>(_chunkVoxelWidth)) * _chunkVoxelWidth;
    const ChunkTerrainData& chunkData = chunk_data_for(chunkOriginX, chunkOriginZ);
    const int localX = terrain_generation::wrap_to_chunk_axis(worldX, _chunkVoxelWidth);
    const int localZ = terrain_generation::wrap_to_chunk_axis(worldZ, _chunkVoxelWidth);
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
    normalize_settings(settings, static_cast<int>(CHUNK_HEIGHT), 1.0f);
    return settings;
}

int TerrainGenerator::sea_level() noexcept
{
    TerrainGenerator& generator = instance();
    return std::clamp(
        world_units_to_voxels(static_cast<float>(generator.settings().shape.seaLevel), generator.block_world_size()),
        0,
        std::max(0, generator.chunk_voxel_height() - 1));
}

int TerrainGenerator::chunk_voxel_width() const noexcept
{
    return _chunkVoxelWidth;
}

int TerrainGenerator::chunk_voxel_height() const noexcept
{
    return _chunkVoxelHeight;
}

float TerrainGenerator::block_world_size() const noexcept
{
    return _blockWorldSize;
}

void TerrainGenerator::apply_settings(const TerrainGeneratorSettings& settings)
{
    std::scoped_lock lock(_stateMutex);
    _settings = settings;
    normalize_settings(_settings, _chunkVoxelHeight, _blockWorldSize);
    const float maxWorldHeight = max_world_height(_chunkVoxelHeight, _blockWorldSize);
    _settings.shape.seaLevel = std::clamp(
        _settings.shape.seaLevel,
        0,
        static_cast<int>(std::floor(maxWorldHeight)));
    _settings.density.maxBandHalfSpanBlocks = std::clamp(
        _settings.density.maxBandHalfSpanBlocks,
        0,
        static_cast<int>(std::floor(maxWorldHeight)));
    for (SplinePoint& point : _settings.erosionSplines)
    {
        point.heightValue = std::clamp(point.heightValue, 0.0f, maxWorldHeight);
    }
    for (SplinePoint& point : _settings.peakSplines)
    {
        point.heightValue = std::clamp(point.heightValue, 0.0f, maxWorldHeight);
    }
    for (SplinePoint& point : _settings.continentalSplines)
    {
        point.heightValue = std::clamp(point.heightValue, 0.0f, maxWorldHeight);
    }
    rebuild_noise();
    _regionCache.clear();
    _columnCache.clear();
}

void TerrainGenerator::set_world_geometry(const int chunkVoxelWidth, const int chunkVoxelHeight, const float blockWorldSize)
{
    std::scoped_lock lock(_stateMutex);
    _chunkVoxelWidth = std::max(1, chunkVoxelWidth);
    _chunkVoxelHeight = std::max(1, chunkVoxelHeight);
    _blockWorldSize = std::max(0.001f, blockWorldSize);
    const float maxWorldHeight = max_world_height(_chunkVoxelHeight, _blockWorldSize);
    _settings.shape.seaLevel = std::clamp(
        _settings.shape.seaLevel,
        0,
        static_cast<int>(std::floor(maxWorldHeight)));
    _settings.density.maxBandHalfSpanBlocks = std::clamp(
        _settings.density.maxBandHalfSpanBlocks,
        0,
        static_cast<int>(std::floor(maxWorldHeight)));
    for (SplinePoint& point : _settings.erosionSplines)
    {
        point.heightValue = std::clamp(point.heightValue, 0.0f, maxWorldHeight);
    }
    for (SplinePoint& point : _settings.peakSplines)
    {
        point.heightValue = std::clamp(point.heightValue, 0.0f, maxWorldHeight);
    }
    for (SplinePoint& point : _settings.continentalSplines)
    {
        point.heightValue = std::clamp(point.heightValue, 0.0f, maxWorldHeight);
    }
    _regionCache.clear();
    _columnCache.clear();
}

void TerrainGenerator::RasterizeChunkTerrain(const WorldGenerationChunkResult& generation, ChunkData& chunkData) const
{
    ZoneScopedN("TerrainGenerator::RasterizeChunkTerrain");
    const int seaLevel = _settings.shape.seaLevel;
    for (int x = 0; x < chunkData.voxelWidth; ++x)
    {
        for (int z = 0; z < chunkData.voxelWidth; ++z)
        {
            for (int y = 0; y < chunkData.voxelHeight; ++y)
            {
                Block& block = chunkData.blocks[x][y][z];
                const TerrainVolumeCell& cell = generation.volumeBuffer.at(x, y, z);
                if (cell.density > 0.0f && cell.material != MaterialClass::Air && cell.material != MaterialClass::Water)
                {
                    terrain_generation::set_solid_block(block, BlockType::STONE);
                }
                else
                {
                    terrain_generation::set_air_or_water_block(seaLevel, y, block);
                }
            }
        }
    }
}

void TerrainGenerator::PopulateBaseTerrainBlocks(const ChunkTerrainData& terrainData, ChunkData& chunkData) const
{
    ZoneScopedN("TerrainGenerator::PopulateBaseTerrainBlocks");
    const int seaLevel = _settings.shape.seaLevel;
    for (int x = 0; x < chunkData.voxelWidth; ++x)
    {
        for (int z = 0; z < chunkData.voxelWidth; ++z)
        {
            const TerrainColumnSample& column = terrainData.at(x, z);
            for (int y = 0; y < chunkData.voxelHeight; ++y)
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
    ZoneScopedN("TerrainGenerator::ApplyVoxelTerrainFeatures");
    PopulateBaseTerrainBlocks(terrainData, chunkData);
}

void TerrainGenerator::RefreshSurfaceMaterials(const ChunkTerrainData&, ChunkData& chunkData) const
{
    ZoneScopedN("TerrainGenerator::RefreshSurfaceMaterials");
    for (int x = 0; x < chunkData.voxelWidth; ++x)
    {
        for (int z = 0; z < chunkData.voxelWidth; ++z)
        {
            for (int y = 0; y < chunkData.voxelHeight; ++y)
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

void TerrainGenerator::normalize_settings(
    TerrainGeneratorSettings& settings,
    const int chunkVoxelHeight,
    const float blockWorldSize)
{
    const float maxWorldHeight = max_world_height(chunkVoxelHeight, blockWorldSize);
    const int maxWorldHeightInt = static_cast<int>(std::floor(maxWorldHeight));
    auto normalize_spline = [maxWorldHeight](std::vector<SplinePoint>& spline, const auto& fallback)
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
            point.heightValue = std::clamp(point.heightValue, 0.0f, maxWorldHeight);
        }
    };

    settings.shape.seaLevel = std::clamp(settings.shape.seaLevel, 0, maxWorldHeightInt);
    normalize_noise_layer(settings.shape.continental);
    normalize_noise_layer(settings.shape.erosion);
    normalize_noise_layer(settings.shape.peaks);
    normalize_noise_layer(settings.shape.weirdness);
    normalize_density_settings(settings.density, maxWorldHeightInt);
    normalize_spline(settings.erosionSplines, DefaultErosionSplines);
    normalize_spline(settings.peakSplines, DefaultPeakSplines);
    normalize_spline(settings.continentalSplines, DefaultContinentalSplines);
}

void TerrainGenerator::rebuild_noise()
{
    _continental = build_noise_layer(_settings.shape.continental);
    _erosion = build_noise_layer(_settings.shape.erosion);
    _peaks = build_noise_layer(_settings.shape.peaks);
    _weirdness = build_noise_layer(_settings.shape.weirdness);
    _density = build_noise_layer(
        TerrainNoiseLayerSettings{
            .basis = _settings.density.basis,
            .frequency = _settings.density.frequency,
            .octaves = _settings.density.octaves,
            .lacunarity = _settings.density.lacunarity,
            .gain = _settings.density.gain,
            .weightedStrength = _settings.density.weightedStrength,
            .remapFromMin = -1.0f,
            .remapFromMax = 1.0f,
            .remapToMin = -1.0f,
            .remapToMax = 1.0f,
            .terraceStepCount = 1,
            .terraceSmoothness = 0.0f,
            .strength = _settings.density.strength
        },
        false);
}
