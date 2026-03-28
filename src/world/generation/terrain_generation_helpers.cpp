#include "terrain_generation_helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
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

    [[nodiscard]] size_t grid2d_index(const int size, const int x, const int z)
    {
        return static_cast<size_t>((z * size) + x);
    }

    [[nodiscard]] float compute_base_height(
        const TerrainNoiseSample& noise,
        const TerrainShapeSettings& shapeSettings,
        const std::vector<SplinePoint>& erosionSplines,
        const std::vector<SplinePoint>& peakSplines,
        const std::vector<SplinePoint>& continentalSplines)
    {
        const float continentalHeight = terrain_generation::sample_spline_height(continentalSplines, noise.continentalness) * shapeSettings.continentalStrength;
        const float peaksHeight = terrain_generation::sample_spline_height(peakSplines, noise.peaksValleys) * shapeSettings.peaksStrength;
        const float erosionHeight = terrain_generation::sample_spline_height(erosionSplines, noise.erosion);
        const float erosionBlend = inverse_lerp(0.0f, static_cast<float>(CHUNK_HEIGHT - 1), erosionHeight);
        const float peakMask = inverse_lerp(-1.0f, 1.0f, noise.peaksValleys);
        const float erosionSuppression = lerp(shapeSettings.erosionSuppressionLow, shapeSettings.erosionSuppressionHigh, erosionBlend);
        const float mountainLift = peaksHeight * lerp(1.0f, erosionSuppression, shapeSettings.erosionStrength);
        const float valleyCarve = (1.0f - peakMask) * (1.0f - erosionBlend) * shapeSettings.valleyStrength;
        const float detailLift = noise.detail * shapeSettings.detailStrength;
        return std::clamp(
            continentalHeight + mountainLift - valleyCarve + detailLift,
            1.0f,
            static_cast<float>(CHUNK_HEIGHT - 8));
    }

    [[nodiscard]] int clamp_surface_height(const float height)
    {
        return std::clamp(
            static_cast<int>(std::round(height)),
            1,
            static_cast<int>(CHUNK_HEIGHT - 8));
    }

    [[nodiscard]] float compute_river_potential(const TerrainShapeSettings& shapeSettings, const float riverNoiseValue)
    {
        if (!shapeSettings.riversEnabled)
        {
            return 0.0f;
        }

        return 1.0f - clamp01(std::abs(riverNoiseValue) / shapeSettings.riverThreshold);
    }

    [[nodiscard]] int apply_river_carve(const TerrainShapeSettings& shapeSettings, const int baseHeight, const float riverPotential)
    {
        if (!shapeSettings.riversEnabled || riverPotential <= 0.0f)
        {
            return baseHeight;
        }

        const float carve = lerp(0.0f, 12.0f, riverPotential * riverPotential);
        return clamp_surface_height(static_cast<float>(baseHeight) - carve);
    }
}

int terrain_generation::floor_to_int(const float value)
{
    return static_cast<int>(std::floor(value));
}

int terrain_generation::wrap_to_chunk_axis(const int value, const int axisSize)
{
    const int mod = value % axisSize;
    return mod < 0 ? mod + axisSize : mod;
}

float terrain_generation::sample_spline_height(const std::vector<SplinePoint>& splinePoints, const float noise)
{
    if (splinePoints.empty())
    {
        return 0.0f;
    }

    for (size_t i = 0; i + 1 < splinePoints.size(); ++i)
    {
        if (noise >= splinePoints[i].noiseValue && noise <= splinePoints[i + 1].noiseValue)
        {
            const float range = splinePoints[i + 1].noiseValue - splinePoints[i].noiseValue;
            const float t = std::abs(range) > std::numeric_limits<float>::epsilon()
                ? (noise - splinePoints[i].noiseValue) / range
                : 0.0f;
            return lerp(splinePoints[i].heightValue, splinePoints[i + 1].heightValue, t);
        }
    }

    if (noise < splinePoints.front().noiseValue)
    {
        return splinePoints.front().heightValue;
    }

    return splinePoints.back().heightValue;
}

void terrain_generation::fill_region_scaffold(
    const FastNoise::SmartNode<>& continentalNoise,
    const FastNoise::SmartNode<>& riverNoise,
    const TerrainGeneratorSettings& settings,
    const glm::ivec2& chunkOrigin,
    WorldRegionScaffold2D& scaffold)
{
    const int size = static_cast<int>(CHUNK_SIZE);
    const int originX = chunkOrigin.x;
    const int originZ = chunkOrigin.y;

    std::vector<float> continentalMap(static_cast<size_t>(size * size));
    std::vector<float> riverMap(static_cast<size_t>(size * size));

    continentalNoise->GenUniformGrid2D(
        continentalMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.continentalFrequency,
        settings.seed);

    if (settings.shape.riversEnabled)
    {
        riverNoise->GenUniformGrid2D(
            riverMap.data(),
            originX,
            originZ,
            size,
            size,
            settings.shape.riverFrequency,
            settings.seed + 303);
    }

    for (int localZ = 0; localZ < size; ++localZ)
    {
        for (int localX = 0; localX < size; ++localX)
        {
            const size_t index = grid2d_index(size, localX, localZ);
            WorldRegionSample& sample = scaffold.at(localX, localZ);
            sample.continentalness = continentalMap[index];
            sample.riverPotential = settings.shape.riversEnabled
                ? compute_river_potential(settings.shape, riverMap[index])
                : 0.0f;
        }
    }
}

void terrain_generation::fill_column_scaffold(
    const FastNoise::SmartNode<>& erosionNoise,
    const FastNoise::SmartNode<>& peaksNoise,
    const FastNoise::SmartNode<>& detailNoise,
    const TerrainGeneratorSettings& settings,
    const WorldRegionScaffold2D& regionScaffold,
    const glm::ivec2& chunkOrigin,
    TerrainColumnScaffold2D& scaffold)
{
    const int size = static_cast<int>(CHUNK_SIZE);
    const int originX = chunkOrigin.x;
    const int originZ = chunkOrigin.y;

    std::vector<float> erosionMap(static_cast<size_t>(size * size));
    std::vector<float> peaksMap(static_cast<size_t>(size * size));
    std::vector<float> detailMap(static_cast<size_t>(size * size));

    erosionNoise->GenUniformGrid2D(
        erosionMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.erosionFrequency,
        settings.seed);
    peaksNoise->GenUniformGrid2D(
        peaksMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.peaksFrequency,
        settings.seed + 101);
    detailNoise->GenUniformGrid2D(
        detailMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.detailFrequency,
        settings.seed + 202);

    for (int localZ = 0; localZ < size; ++localZ)
    {
        for (int localX = 0; localX < size; ++localX)
        {
            const size_t index = grid2d_index(size, localX, localZ);
            const WorldRegionSample& region = regionScaffold.at(localX, localZ);
            TerrainColumnSample& column = scaffold.at(localX, localZ);

            column.noise = TerrainNoiseSample{
                .continentalness = region.continentalness,
                .erosion = erosionMap[index],
                .peaksValleys = peaksMap[index],
                .detail = detailMap[index],
                .river = settings.shape.riversEnabled ? (1.0f - (region.riverPotential * 2.0f)) : -1.0f
            };

            const int baseHeight = clamp_surface_height(compute_base_height(
                column.noise,
                settings.shape,
                settings.erosionSplines,
                settings.peakSplines,
                settings.continentalSplines));
            const int carvedHeight = apply_river_carve(settings.shape, baseHeight, region.riverPotential);

            column.baseSurfaceHeight = baseHeight;
            column.surfaceHeight = carvedHeight;
            column.stoneHeight = carvedHeight;
            column.biome = BiomeType::None; //TODO: I guess this should ust be a place holder until biome classification is implemented?
            column.topBlock = BlockType::STONE;
            column.fillerBlock = BlockType::STONE;
            column.hasRiver = settings.shape.riversEnabled && region.riverPotential > 0.58f;
            column.isBeach = false;
        }
    }
}

void terrain_generation::fill_heightfield_volume(
    const TerrainColumnScaffold2D& columnScaffold,
    const glm::ivec2& chunkOrigin,
    TerrainVolumeBuffer& volumeBuffer)
{
    volumeBuffer.chunkOrigin = chunkOrigin;
    for (int localZ = 0; localZ < static_cast<int>(CHUNK_SIZE); ++localZ)
    {
        for (int localX = 0; localX < static_cast<int>(CHUNK_SIZE); ++localX)
        {
            const TerrainColumnSample& column = columnScaffold.at(localX, localZ);
            for (int y = 0; y < static_cast<int>(CHUNK_HEIGHT); ++y)
            {
                TerrainVolumeCell& cell = volumeBuffer.at(localX, y, localZ);
                if (y <= column.surfaceHeight)
                {
                    cell.density = static_cast<float>(column.surfaceHeight + 1 - y);
                    cell.material = MaterialClass::Stone;
                    cell.featureId = 0u;
                    cell.surfaceAffinity = y == column.surfaceHeight ? 1.0f : 0.0f;
                }
                else
                {
                    cell.density = -1.0f;
                    cell.material = MaterialClass::Air;
                    cell.featureId = 0u;
                    cell.surfaceAffinity = 0.0f;
                }
            }
        }
    }
}

void terrain_generation::clear_surface_classification(SurfaceClassificationBuffer& surfaceBuffer)
{
    surfaceBuffer.faces.assign(
        static_cast<size_t>(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE),
        std::array<SurfaceClass, 6>{});
}

void terrain_generation::clear_appearance(AppearanceBuffer& appearanceBuffer)
{
    appearanceBuffer.voxels.assign(
        static_cast<size_t>(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE),
        TerrainAppearanceVoxel{});
}

void terrain_generation::set_air_or_water_block(const int seaLevel, const int y, Block& block)
{
    block._solid = false;
    block._type = y <= seaLevel ? BlockType::WATER : BlockType::AIR;
    block._sunlight = 0;
    block._localLight = {};
}

void terrain_generation::set_solid_block(Block& block, const BlockType type)
{
    block._solid = true;
    block._type = type;
    block._sunlight = 0;
    block._localLight = {};
}
