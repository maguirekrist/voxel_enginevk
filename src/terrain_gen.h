#pragma once

#include "FastNoise/Generators/Simplex.h"
#include "FastNoise/SmartNode.h"
#include <vk_types.h>
#include <FastNoise/FastNoise.h>

constexpr float RIVER_THRESHOLD = -0.2f;
constexpr int RIVER_HEIGHT = 50;

class TerrainGenerator {
public:

    static std::vector<int> generate_height_map(int xStart, int zStart);
    static float get_normalized_height(std::vector<float>& map, int yScale, int xScale, int x, int y);

private:
    static int _seed;
    static FastNoise::GeneratorSource _generator;

    static void add_rivers(std::vector<int>& heightMap, int chunkX, int chunkZ);

    static FastNoise::SmartNode<FastNoise::Perlin> _terrainNoise;
    static FastNoise::SmartNode<FastNoise::Simplex> _caveNoise;
    static FastNoise::SmartNode<FastNoise::Perlin> _riverNoise;
};