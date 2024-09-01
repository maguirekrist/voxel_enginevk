
#pragma once
#include <vk_types.h>
#include <random.h>
#include <chunk.h>
#include <FastNoise/FastNoise.h>

class World {
public:
    std::vector<std::unique_ptr<Chunk>> _chunks;
    int _seed;
    FastNoise::GeneratorSource _generator;

    World();
private:
    void generate_chunk(int xStart, int yStart);
    void update_chunk(Chunk& chunk, std::vector<float>& heightMap);
};