#include "terrain_generation_helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tracy/Tracy.hpp>

namespace
{
    struct DensityBandRange
    {
        float bandHalfSpan{};
        float surfaceCenter{};
        int noisyStartY{};
        int noisyEndY{};
    };

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

    [[nodiscard]] size_t grid3d_index(const int xSize, const int ySize, const int x, const int y, const int z)
    {
        return static_cast<size_t>(((z * ySize) + y) * xSize + x);
    }

    [[nodiscard]] int floor_divide(const int value, const int divisor)
    {
        int quotient = value / divisor;
        const int remainder = value % divisor;
        if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
        {
            --quotient;
        }

        return quotient;
    }

    [[nodiscard]] int ceil_divide(const int value, const int divisor)
    {
        int quotient = value / divisor;
        const int remainder = value % divisor;
        if (remainder != 0 && ((remainder < 0) == (divisor < 0)))
        {
            ++quotient;
        }

        return quotient;
    }

    [[nodiscard]] int world_units_to_voxel_stride(const float worldUnits, const float blockWorldSize)
    {
        return std::max(1, static_cast<int>(std::lround(worldUnits / blockWorldSize)));
    }

    [[nodiscard]] float sample_trilinear_lattice(
        const std::vector<float>& values,
        const int sizeX,
        const int sizeY,
        const int sizeZ,
        const int x0,
        const int y0,
        const int z0,
        const float tx,
        const float ty,
        const float tz)
    {
        const int x1 = std::min(x0 + 1, sizeX - 1);
        const int y1 = std::min(y0 + 1, sizeY - 1);
        const int z1 = std::min(z0 + 1, sizeZ - 1);

        const float c000 = values[grid3d_index(sizeX, sizeY, x0, y0, z0)];
        const float c100 = values[grid3d_index(sizeX, sizeY, x1, y0, z0)];
        const float c010 = values[grid3d_index(sizeX, sizeY, x0, y1, z0)];
        const float c110 = values[grid3d_index(sizeX, sizeY, x1, y1, z0)];
        const float c001 = values[grid3d_index(sizeX, sizeY, x0, y0, z1)];
        const float c101 = values[grid3d_index(sizeX, sizeY, x1, y0, z1)];
        const float c011 = values[grid3d_index(sizeX, sizeY, x0, y1, z1)];
        const float c111 = values[grid3d_index(sizeX, sizeY, x1, y1, z1)];

        const float c00 = lerp(c000, c100, tx);
        const float c10 = lerp(c010, c110, tx);
        const float c01 = lerp(c001, c101, tx);
        const float c11 = lerp(c011, c111, tx);
        const float c0 = lerp(c00, c10, ty);
        const float c1 = lerp(c01, c11, ty);
        return lerp(c0, c1, tz);
    }

    [[nodiscard]] float compute_surface_height(
        const TerrainNoiseSample& noise,
        const TerrainShapeSettings& shapeSettings,
        const std::vector<SplinePoint>& erosionSplines,
        const std::vector<SplinePoint>& peakSplines,
        const std::vector<SplinePoint>& continentalSplines,
        const int chunkVoxelHeight,
        const float blockWorldSize)
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
            return static_cast<float>(shapeSettings.seaLevel) / blockWorldSize;
        }

        const float weightedHeight =
            (continentalHeight * shapeSettings.continental.strength) +
            (peaksHeight * shapeSettings.peaks.strength) +
            (erosionHeight * shapeSettings.erosion.strength);

        return std::clamp(
            (weightedHeight / totalStrength) / blockWorldSize,
            1.0f,
            static_cast<float>(chunkVoxelHeight - 8));
    }

    [[nodiscard]] int clamp_surface_height(const float height, const int chunkVoxelHeight)
    {
        return std::clamp(
            static_cast<int>(std::round(height)),
            1,
            chunkVoxelHeight - 8);
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
    const float blockWorldSize,
    WorldRegionScaffold2D& scaffold)
{
    ZoneScopedN("terrain_generation::fill_region_scaffold");
    const int size = scaffold.chunkVoxelWidth;
    const int originX = chunkOrigin.x;
    const int originZ = chunkOrigin.y;

    std::vector<float> continentalMap(static_cast<size_t>(size * size));
    continentalNoise->GenUniformGrid2D(
        continentalMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.continental.frequency * blockWorldSize,
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
    const float blockWorldSize,
    TerrainColumnScaffold2D& scaffold)
{
    ZoneScopedN("terrain_generation::fill_column_scaffold");
    const int size = scaffold.chunkVoxelWidth;
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
        settings.shape.erosion.frequency * blockWorldSize,
        settings.seed);
    peaksNoise->GenUniformGrid2D(
        peaksMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.peaks.frequency * blockWorldSize,
        settings.seed + 101);
    weirdnessNoise->GenUniformGrid2D(
        weirdnessMap.data(),
        originX,
        originZ,
        size,
        size,
        settings.shape.weirdness.frequency * blockWorldSize,
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
                settings.continentalSplines,
                scaffold.chunkVoxelHeight,
                blockWorldSize),
                scaffold.chunkVoxelHeight);

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
    const float blockWorldSize,
    TerrainVolumeBuffer& volumeBuffer)
{
    ZoneScopedN("terrain_generation::fill_density_volume");
    volumeBuffer.chunkOrigin = chunkOrigin;
    const int chunkVoxelWidth = volumeBuffer.chunkVoxelWidth;
    const int chunkVoxelHeight = volumeBuffer.chunkVoxelHeight;
    const float densityFrequency = settings.density.frequency * blockWorldSize;
    const int densitySampleStride = world_units_to_voxel_stride(settings.density.sampleCellSizeBlocks, blockWorldSize);
    const bool densityNoiseEnabled =
        settings.density.strength > std::numeric_limits<float>::epsilon() &&
        densityFrequency > std::numeric_limits<float>::epsilon();
    std::vector<DensityBandRange> columnBands(static_cast<size_t>(chunkVoxelWidth * chunkVoxelWidth));
    int minNoisyStartY = chunkVoxelHeight;
    int maxNoisyEndY = -1;

    {
        ZoneScopedN("terrain_generation::ComputeDensityBands");
        for (int localZ = 0; localZ < chunkVoxelWidth; ++localZ)
        {
            for (int localX = 0; localX < chunkVoxelWidth; ++localX)
            {
                const TerrainColumnSample& column = columnScaffold.at(localX, localZ);
                const float normalizedWeirdness = clamp01(
                    ((column.noise.weirdness + 1.0f) * 0.5f) * settings.shape.weirdness.strength);
                const float bandHalfSpan =
                    normalizedWeirdness *
                    (static_cast<float>(settings.density.maxBandHalfSpanBlocks) / blockWorldSize);
                const float surfaceCenter = static_cast<float>(column.surfaceHeight) + 0.5f;
                const float lowerEdge = surfaceCenter - bandHalfSpan;
                const float upperEdge = surfaceCenter + bandHalfSpan;

                DensityBandRange& band = columnBands[grid2d_index(chunkVoxelWidth, localX, localZ)];
                band.bandHalfSpan = bandHalfSpan;
                band.surfaceCenter = surfaceCenter;
                band.noisyStartY = std::clamp(
                    static_cast<int>(std::ceil(lowerEdge - 0.5f)),
                    0,
                    chunkVoxelHeight);
                band.noisyEndY = std::clamp(
                    static_cast<int>(std::floor(upperEdge - 0.5f)),
                    -1,
                    chunkVoxelHeight - 1);

                if (!densityNoiseEnabled || band.bandHalfSpan <= std::numeric_limits<float>::epsilon())
                {
                    continue;
                }

                const int noisyStartY = std::max(0, band.noisyStartY);
                const int noisyEndY = std::min(chunkVoxelHeight - 1, band.noisyEndY);
                if (noisyEndY >= noisyStartY)
                {
                    minNoisyStartY = std::min(minNoisyStartY, noisyStartY);
                    maxNoisyEndY = std::max(maxNoisyEndY, noisyEndY);
                }
            }
        }
    }

    std::vector<float> densitySamples{};
    int latticeStartX = chunkOrigin.x;
    int latticeStartY = minNoisyStartY;
    int latticeStartZ = chunkOrigin.y;
    int latticeSizeX = 0;
    int latticeSizeY = 0;
    int latticeSizeZ = 0;
    if (densityNoiseEnabled && maxNoisyEndY >= minNoisyStartY)
    {
        {
            ZoneScopedN("terrain_generation::SampleDensityNoiseBatch");
            ZoneText("coarse_trilinear", 16);
            const int chunkWorldMaxX = chunkOrigin.x + chunkVoxelWidth - 1;
            const int chunkWorldMaxZ = chunkOrigin.y + chunkVoxelWidth - 1;
            latticeStartX = floor_divide(chunkOrigin.x, densitySampleStride) * densitySampleStride;
            latticeStartY = floor_divide(minNoisyStartY, densitySampleStride) * densitySampleStride;
            latticeStartZ = floor_divide(chunkOrigin.y, densitySampleStride) * densitySampleStride;
            const int latticeEndX = ceil_divide(chunkWorldMaxX, densitySampleStride) * densitySampleStride;
            const int latticeEndY = ceil_divide(maxNoisyEndY, densitySampleStride) * densitySampleStride;
            const int latticeEndZ = ceil_divide(chunkWorldMaxZ, densitySampleStride) * densitySampleStride;

            latticeSizeX = ((latticeEndX - latticeStartX) / densitySampleStride) + 1;
            latticeSizeY = ((latticeEndY - latticeStartY) / densitySampleStride) + 1;
            latticeSizeZ = ((latticeEndZ - latticeStartZ) / densitySampleStride) + 1;
            densitySamples.resize(static_cast<size_t>(latticeSizeX * latticeSizeY * latticeSizeZ));
            densityNoise->GenUniformGrid3D(
                densitySamples.data(),
                latticeStartX / densitySampleStride,
                latticeStartY / densitySampleStride,
                latticeStartZ / densitySampleStride,
                latticeSizeX,
                latticeSizeY,
                latticeSizeZ,
                densityFrequency * static_cast<float>(densitySampleStride),
                settings.seed + 907);
        }
    }

    {
        ZoneScopedN("terrain_generation::WriteDensityCells");
        for (int localZ = 0; localZ < chunkVoxelWidth; ++localZ)
        {
            const int worldZ = chunkOrigin.y + localZ;
            const int latticeRelZ = worldZ - latticeStartZ;
            const int latticeZ0 = latticeSizeZ > 0 ? latticeRelZ / densitySampleStride : 0;
            const float tz = latticeSizeZ > 0
                ? static_cast<float>(latticeRelZ % densitySampleStride) / static_cast<float>(densitySampleStride)
                : 0.0f;
            for (int localX = 0; localX < chunkVoxelWidth; ++localX)
            {
                const TerrainColumnSample& column = columnScaffold.at(localX, localZ);
                const DensityBandRange& band = columnBands[grid2d_index(chunkVoxelWidth, localX, localZ)];
                const int worldX = chunkOrigin.x + localX;
                const int latticeRelX = worldX - latticeStartX;
                const int latticeX0 = latticeSizeX > 0 ? latticeRelX / densitySampleStride : 0;
                const float tx = latticeSizeX > 0
                    ? static_cast<float>(latticeRelX % densitySampleStride) / static_cast<float>(densitySampleStride)
                    : 0.0f;

                auto set_solid_cell = [&](TerrainVolumeCell& cell, const int y, const bool nearSurface)
                {
                    cell.density = nearSurface ? static_cast<float>(column.surfaceHeight + 1 - y) : 1.0f;
                    cell.material = MaterialClass::Stone;
                    cell.featureId = 0u;
                    cell.surfaceAffinity = nearSurface ? 1.0f : 0.0f;
                };

                auto set_air_cell = [&](TerrainVolumeCell& cell)
                {
                    cell.density = -1.0f;
                    cell.material = MaterialClass::Air;
                    cell.featureId = 0u;
                    cell.surfaceAffinity = 0.0f;
                };

                if (band.bandHalfSpan <= std::numeric_limits<float>::epsilon())
                {
                    for (int y = 0; y < chunkVoxelHeight; ++y)
                    {
                        TerrainVolumeCell& cell = volumeBuffer.at(localX, y, localZ);
                        if (y <= column.surfaceHeight)
                        {
                            set_solid_cell(cell, y, y == column.surfaceHeight);
                        }
                        else
                        {
                            set_air_cell(cell);
                        }
                    }
                    continue;
                }

                const int solidEndY = std::min(chunkVoxelHeight - 1, band.noisyStartY - 1);
                for (int y = 0; y <= solidEndY; ++y)
                {
                    set_solid_cell(volumeBuffer.at(localX, y, localZ), y, false);
                }

                const int noisyStartY = std::max(0, band.noisyStartY);
                const int noisyEndY = std::min(chunkVoxelHeight - 1, band.noisyEndY);
                for (int y = noisyStartY; y <= noisyEndY; ++y)
                {
                    TerrainVolumeCell& cell = volumeBuffer.at(localX, y, localZ);
                    const float sampleCenter = static_cast<float>(y) + 0.5f;
                    const float verticalDistance = std::abs(sampleCenter - band.surfaceCenter);
                    const float normalizedDistance = clamp01(verticalDistance / band.bandHalfSpan);
                    const float noiseWeight = 1.0f - normalizedDistance;
                    const float surfaceAnchor = (band.surfaceCenter - sampleCenter) / band.bandHalfSpan;
                    float densitySample = 0.0f;
                    if (densityNoiseEnabled)
                    {
                        const int latticeRelY = y - latticeStartY;
                        const int latticeY0 = latticeRelY / densitySampleStride;
                        const float ty = static_cast<float>(latticeRelY % densitySampleStride) / static_cast<float>(densitySampleStride);
                        densitySample = sample_trilinear_lattice(
                            densitySamples,
                            latticeSizeX,
                            latticeSizeY,
                            latticeSizeZ,
                            latticeX0,
                            latticeY0,
                            latticeZ0,
                            tx,
                            ty,
                            tz);
                    }
                    const float densityValue =
                        surfaceAnchor +
                        (densitySample * settings.density.strength * noiseWeight);

                    cell.density = densityValue;
                    cell.featureId = 0u;
                    cell.surfaceAffinity = clamp01(1.0f - std::abs(band.surfaceCenter - sampleCenter));
                    cell.material = densityValue > 0.0f ? MaterialClass::Stone : MaterialClass::Air;
                }

                for (int y = std::max(noisyEndY + 1, 0); y < chunkVoxelHeight; ++y)
                {
                    set_air_cell(volumeBuffer.at(localX, y, localZ));
                }
            }
        }
    }
}

void terrain_generation::clear_surface_classification(SurfaceClassificationBuffer& surfaceBuffer)
{
    surfaceBuffer.faces.assign(
        static_cast<size_t>(surfaceBuffer.chunkVoxelWidth * surfaceBuffer.chunkVoxelHeight * surfaceBuffer.chunkVoxelWidth),
        std::array<SurfaceClass, 6>{});
}

void terrain_generation::clear_appearance(AppearanceBuffer& appearanceBuffer)
{
    appearanceBuffer.voxels.assign(
        static_cast<size_t>(appearanceBuffer.chunkVoxelWidth * appearanceBuffer.chunkVoxelHeight * appearanceBuffer.chunkVoxelWidth),
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
