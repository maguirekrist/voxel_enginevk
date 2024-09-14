#pragma once

#include "FastNoise/Generators/Simplex.h"
#include "FastNoise/SmartNode.h"
#include <vk_types.h>
#include <FastNoise/FastNoise.h>
#include "chunk.h"
#include "random.h"

constexpr float RIVER_THRESHOLD = -0.2f;
constexpr int RIVER_HEIGHT = 50;

class TerrainGenerator {
public:
    TerrainGenerator();
    void GenerateChunk(int chunkX, int chunkZ, std::vector<uint8_t>& blockData);

private:
    // Noise generators
    FastNoise::SmartNode<FastNoise::Perlin> baseTerrainNoise;
    FastNoise::SmartNode<FastNoise::Perlin> mountainNoise;
    FastNoise::SmartNode<FastNoise::Perlin> riverNoise;
    FastNoise::SmartNode<FastNoise::Simplex> caveNoise;

    int _seed = Random::generate(0, 1337);


    // Helper functions
    int GetBaseHeight(int x, int z);
    float GetMountainHeight(int x, int z);
    bool IsRiver(int x, int z);
    bool IsCave(int x, int y, int z);
};
