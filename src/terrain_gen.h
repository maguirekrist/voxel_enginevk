#pragma once

#include <vk_types.h>
#include <FastNoise/FastNoise.h>
#include "random.h"



class TerrainGenerator {
public:
    static TerrainGenerator& instance()
    {
        static TerrainGenerator *instance = new TerrainGenerator();
        return *instance;
    }

    void GenerateChunk(int chunkX, int chunkZ, std::vector<uint8_t>& blockData);

    std::vector<float> GenerateHeightMap(int chunkX, int chunkZ);
    float NormalizeHeight(std::vector<float>& map, int yScale, int xScale, int x, int y);
private:
    TerrainGenerator() {
        _seed = Random::generate(0, 1337);
        baseTerrainNoise = FastNoise::New<FastNoise::Perlin>();
        mountainNoise = FastNoise::New<FastNoise::Perlin>();
        riverNoise = FastNoise::New<FastNoise::Perlin>();
        caveNoise = FastNoise::New<FastNoise::Simplex>();
    }

    // Noise generators
    FastNoise::SmartNode<FastNoise::Perlin> baseTerrainNoise;
    FastNoise::SmartNode<FastNoise::Perlin> mountainNoise;
    FastNoise::SmartNode<FastNoise::Perlin> riverNoise;
    FastNoise::SmartNode<FastNoise::Simplex> caveNoise;
    

    int _seed;



    // Helper functions
    int GetBaseHeight(int x, int z);
    float GetMountainHeight(int x, int z);
    bool IsRiver(int x, int z);
    bool IsCave(int x, int y, int z);
};
