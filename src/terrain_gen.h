#pragma once

#include <vk_types.h>
#include <FastNoise/FastNoise.h>
#include "random.h"

struct SplinePoint {
    float noiseValue;
    float heightValue;
};

constexpr float lerp(float startValue, float endValue, float t) {
    return (1 - t) * startValue + t * endValue;
}

class TerrainGenerator {
public:
    static TerrainGenerator& instance()
    {
        static TerrainGenerator *instance = new TerrainGenerator();
        return *instance;
    }

    // void GenerateChunk(int chunkX, int chunkZ, std::vector<uint8_t>& blockData);
    std::vector<float> GenerateHeightMap(int chunkX, int chunkZ);
    //std::vector<float> GenerateDensityMap(int chunkX, int chunkZ);
    //float SampleNoise3D(int x, int y, int z);
    float NormalizeHeight(std::vector<float>& map, int yScale, int xScale, int x, int y);
private:
    TerrainGenerator() {
        _seed = Random::generate(0, 1337);

        _erosion = FastNoise::NewFromEncodedNodeTree("FwAAAAAAexQuv83MTL5mZgbAIgApXJNBZmZmvxAAbxKDOg0ACAAAAArXI0AHAAB7FC4/AAAAgD8Aw/XwQQ==");
        _peaks = FastNoise::NewFromEncodedNodeTree("EwCamRk/IgDXo3A/zcwMQBcAuB6FPpqZqcA9Cte+w/XoQCIAexS+QK5HwUAQAIXr0UAPAAkAAABcjyJACQAAXI/CPgB7FK4/AI/C9T0=");
        _continental = FastNoise::NewFromEncodedNodeTree("FwAK16M8w/Wov7geBb8UrkdAIgCkcA1BCtejPA0ABAAAALgeZUAIAAAAAAA/AFyPwj8=");
    }   

    // Noise generators
    FastNoise::SmartNode<> _erosion;
    FastNoise::SmartNode<> _peaks;
    FastNoise::SmartNode<> _continental;

    std::vector<SplinePoint> _erosionSplines = {
        { -1.0f, 50.0f },
        { 0.3f, 100.0f },
        { 0.4f, 150.0f },
        { 1.0f, 150.0f }
    };

    std::vector<SplinePoint> _peakSplines = {
        { -1.0f, 0.0f },
        { -0.6f, 20.0f },
        { -0.2f, 30.0f },
        { 0.0f, 30.0f },
        { 0.2f, 120.0f },
        { 1.0f, 150.0f }
    };

    std::vector<SplinePoint> _continentalSplines = {
        {-1.0f, 0.0f },
        { -0.5f, 0.0f },
        { -0.25f, 20.0f },
        { 0.0f, 30.0f },
        { 0.1f, 60.0f },
        { 0.5f, 70.0f },
        { 1.0f, 90.0f }
    };

    int _seed;

    float map_height(float noise, const std::vector<SplinePoint>& splinePoints);

};
