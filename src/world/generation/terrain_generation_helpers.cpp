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

    [[nodiscard]] float compute_surface_height(
        const TerrainNoiseSample& noise,
        const TerrainShapeSettings& shapeSettings,
        const std::vector<SplinePoint>& erosionSplines,
        const std::vector<SplinePoint>& peakSplines,
        const std::vector<SplinePoint>& continentalSplines)
    {
        const float continentalHeight = terrain_generation::sample_spline_height(continentalSplines, noise.continentalness);
        const float peaksHeight = terrain_generation::sample_spline_height(peakSplines, noise.peaksValleys);
        const float erosionHeight = terrain_generation::sample_spline_height(erosionSplines, noise.erosion);
        const float totalStrength =
            shapeSettings.continental.strength +
            shapeSettings.peaks.strength +
            shapeSettings.erosion.strength;

        if (totalStrength <= std::numeric_limits<float>::epsilon())
        {
            return static_cast<float>(shapeSettings.seaLevel);
        }

        const float weightedHeight =
            (continentalHeight * shapeSettings.continental.strength) +
            (peaksHeight * shapeSettings.peaks.strength) +
            (erosionHeight * shapeSettings.erosion.strength);

        return std::clamp(
            weightedHeight / totalStrength,
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
    const TerrainGeneratorSettings& settings,
    const glm::ivec2& chunkOrigin,
    WorldRegionScaffold2D& scaffold)
{
    const int size = static_cast<int>(CHUNK_SIZE);
    const int originX = chunkOrigin.x;
    const int originZ = chunkOrigin.y;

    std::vector<float> continentalMap(static_cast<size_t>(size * size));
    continentalNoise->GenUniformGrid2D(
        continentalMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.continental.frequency,
        settings.seed);

    for (int localZ = 0; localZ < size; ++localZ)
    {
        for (int localX = 0; localX < size; ++localX)
        {
            const size_t index = grid2d_index(size, localX, localZ);
            WorldRegionSample& sample = scaffold.at(localX, localZ);
            sample.continentalness = continentalMap[index];
        }
    }
}

void terrain_generation::fill_column_scaffold(
    const FastNoise::SmartNode<>& erosionNoise,
    const FastNoise::SmartNode<>& peaksNoise,
    const FastNoise::SmartNode<>& weirdnessNoise,
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
    std::vector<float> weirdnessMap(static_cast<size_t>(size * size));
    erosionNoise->GenUniformGrid2D(
        erosionMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.erosion.frequency,
        settings.seed);
    peaksNoise->GenUniformGrid2D(
        peaksMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.peaks.frequency,
        settings.seed + 101);
    weirdnessNoise->GenUniformGrid2D(
        weirdnessMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.weirdness.frequency,
        settings.seed + 211);

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
                .weirdness = weirdnessMap[index]
            };

            const int shapedHeight = clamp_surface_height(compute_surface_height(
                column.noise,
                settings.shape,
                settings.erosionSplines,
                settings.peakSplines,
                settings.continentalSplines));

            column.surfaceHeight = shapedHeight;
            column.stoneHeight = shapedHeight;
            column.biome = BiomeType::None; //TODO: I guess this should ust be a place holder until biome classification is implemented?
            column.topBlock = BlockType::STONE;
            column.fillerBlock = BlockType::STONE;
            column.isBeach = false;
        }
    }
}

void terrain_generation::fill_density_volume(
    const TerrainColumnScaffold2D& columnScaffold,
    const FastNoise::SmartNode<>& densityNoise,
    const TerrainGeneratorSettings& settings,
    const glm::ivec2& chunkOrigin,
    TerrainVolumeBuffer& volumeBuffer)
{
    volumeBuffer.chunkOrigin = chunkOrigin;
    std::vector<float> densitySamples(static_cast<size_t>(CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE));
    densityNoise->GenUniformGrid3D(
        densitySamples.data(),
        chunkOrigin.x,
        0,
        chunkOrigin.y,
        static_cast<int>(CHUNK_SIZE),
        static_cast<int>(CHUNK_HEIGHT),
        static_cast<int>(CHUNK_SIZE),
        settings.density.frequency,
        settings.seed + 907);

    for (int localZ = 0; localZ < static_cast<int>(CHUNK_SIZE); ++localZ)
    {
        for (int localX = 0; localX < static_cast<int>(CHUNK_SIZE); ++localX)
        {
            const TerrainColumnSample& column = columnScaffold.at(localX, localZ);
            const float normalizedWeirdness = clamp01(
                ((column.noise.weirdness + 1.0f) * 0.5f) * settings.shape.weirdness.strength);
            const float bandHalfSpan = normalizedWeirdness * static_cast<float>(settings.density.maxBandHalfSpanBlocks);
            const float surfaceCenter = static_cast<float>(column.surfaceHeight) + 0.5f;
            const float lowerEdge = surfaceCenter - bandHalfSpan;
            const float upperEdge = surfaceCenter + bandHalfSpan;
            for (int y = 0; y < static_cast<int>(CHUNK_HEIGHT); ++y)
            {
                TerrainVolumeCell& cell = volumeBuffer.at(localX, y, localZ);
                const float sampleCenter = static_cast<float>(y) + 0.5f;
                const size_t densityIndex = static_cast<size_t>(
                    ((localZ * static_cast<int>(CHUNK_HEIGHT) + y) * static_cast<int>(CHUNK_SIZE)) + localX);

                if (bandHalfSpan <= std::numeric_limits<float>::epsilon())
                {
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
                    continue;
                }

                if (sampleCenter < lowerEdge)
                {
                    cell.density = 1.0f;
                    cell.material = MaterialClass::Stone;
                    cell.featureId = 0u;
                    cell.surfaceAffinity = 0.0f;
                    continue;
                }

                if (sampleCenter > upperEdge)
                {
                    cell.density = -1.0f;
                    cell.material = MaterialClass::Air;
                    cell.featureId = 0u;
                    cell.surfaceAffinity = 0.0f;
                    continue;
                }

                const float verticalDistance = std::abs(sampleCenter - surfaceCenter);
                const float normalizedDistance = clamp01(verticalDistance / bandHalfSpan);
                const float noiseWeight = 1.0f - normalizedDistance;
                const float surfaceAnchor = (surfaceCenter - sampleCenter) / bandHalfSpan;
                const float densityValue =
                    surfaceAnchor +
                    (densitySamples[densityIndex] * settings.density.strength * noiseWeight);

                cell.density = densityValue;
                cell.featureId = 0u;
                cell.surfaceAffinity = clamp01(1.0f - std::abs(surfaceCenter - sampleCenter));
                if (densityValue > 0.0f)
                {
                    cell.material = MaterialClass::Stone;
                }
                else
                {
                    cell.material = MaterialClass::Air;
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
