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
    std::vector<float> GenerateDensityMap(int chunkX, int chunkZ);
    float SampleNoise3D(int x, int y, int z);
    float NormalizeHeight(std::vector<float>& map, int yScale, int xScale, int x, int y);
private:
    TerrainGenerator() {
        _seed = Random::generate(0, 1337);
        baseTerrainNoise = FastNoise::New<FastNoise::Perlin>();
        mountainNoise = FastNoise::New<FastNoise::Perlin>();
        riverNoise = FastNoise::New<FastNoise::Perlin>();
        caveNoise = FastNoise::New<FastNoise::Simplex>();

        auto frequency = FastNoise::New<FastNoise::Constant>();
        frequency->SetValue(0.15f);
        auto perlinWithFreq = FastNoise::New<FastNoise::Multiply>();
        perlinWithFreq->SetLHS(frequency);
        perlinWithFreq->SetRHS(baseTerrainNoise);
        _generator = perlinWithFreq;
    }

    FastNoise::SmartNode<FastNoise::Multiply> _generator;

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
