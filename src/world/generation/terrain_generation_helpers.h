#pragma once

#include <FastNoise/FastNoise.h>

#include "../terrain_gen.h"

struct Block;

namespace terrain_generation
{
    [[nodiscard]] int floor_to_int(float value);
    [[nodiscard]] int wrap_to_chunk_axis(int value, int axisSize);
    [[nodiscard]] float sample_spline_height(const std::vector<SplinePoint>& splinePoints, float noise);

    void fill_region_scaffold(
        const FastNoise::SmartNode<>& continentalNoise,
        const TerrainGeneratorSettings& settings,
        const glm::ivec2& chunkOrigin,
        WorldRegionScaffold2D& scaffold);

    void fill_column_scaffold(
        const FastNoise::SmartNode<>& erosionNoise,
        const FastNoise::SmartNode<>& peaksNoise,
        const TerrainGeneratorSettings& settings,
        const WorldRegionScaffold2D& regionScaffold,
        const glm::ivec2& chunkOrigin,
        TerrainColumnScaffold2D& scaffold);

    void fill_heightfield_volume(
        const TerrainColumnScaffold2D& columnScaffold,
        const glm::ivec2& chunkOrigin,
        TerrainVolumeBuffer& volumeBuffer);

    void clear_surface_classification(SurfaceClassificationBuffer& surfaceBuffer);
    void clear_appearance(AppearanceBuffer& appearanceBuffer);

    void set_air_or_water_block(int seaLevel, int y, Block& block);
    void set_solid_block(Block& block, BlockType type);
}
